 //
//  main.c
//  client
//
//  Created by Dante Mattson on 4/8/20.
//  Copyright © 2020 Dante Mattson. All rights reserved.
//

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <stddef.h>
#include <sys/stat.h>

#define PORT 8080
#define BUFLEN 512
#define FILEBUFLEN 40960

// Zombie termination
void sig_child(int signum) {
    pid_t pid;
    int stat;
    // -1 means to wait for first child to terminate.
    // WNOHANG tells the kernel not to block if there are no terminated children.
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        printf("child %d terminated\n", pid);
    }
    
    return;
}

// Separates commands
// input: "in p ut" -> ["in", "p", "ut"]; k = 3
char** separateCommands(char * input, int* nk) {
    char *inputCopy = input;
    char delim[5] = " \n\0";
    printf("input %s\n", input);
    static char *commands[64] = {0, };

    int k = 0;

    // Seperate the input into an array of commands
    commands[k] = strtok(inputCopy, delim);
    
    while( commands[k] != NULL ) {
        k++;
        commands[k] = strtok(NULL, delim);
    }
    
    // assign k and return
    *nk = k;
    return commands;
}

// Sends messages to the server and handles any errors
void sendToServer(int ConnectSocket, char *buffer, int buflen)
{
    int iSendResult = (int) send(ConnectSocket, buffer, buflen, 0);
    if (iSendResult < 0 ){
        perror("send failed with error:");
        close(ConnectSocket);
        exit(1);
    }
}

// Receives messages from the client and handles errors
char* receive(int ConnectSocket, int iResult, char *recvbuf, int recvbuflen) {
    
    iResult = (int) recv(ConnectSocket, recvbuf, recvbuflen, 0);
    if (iResult > 0){
        printf("Bytes Received: %d\n", iResult);
        return recvbuf;
    }
    else if (iResult == 0){
        printf("Connection Closed\n");
    }
    else if (iResult == -1) {
        perror("Receive Failed with error\n");
    }
    
    return "Unable to read\n";
}

// 1 = file, 0 = dir
int isFileOrDir(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

// Runs the put command, reads and uploads files to the server
void put(int ConnectSocket, char *inputCopy, int inputSize, char **commands, int k) {
    // check files exist before sending request
    // -1 for put, -1 for dirname
    int filesExpectedToSend = k - 2;
    int shouldOverride = 0;
    if (strcmp(commands[k - 1], "-f") == 0) {
        // -1 for -f
        shouldOverride = 1;
        filesExpectedToSend -= 1;
    }
    FILE *fileRead;
    int fileExistsCount = 0;
    
    // Check all the files
    for (int i = 2; i < 2 + filesExpectedToSend; i++) {
        printf("reading file: %s\n", commands[i]);
        fileRead = fopen(commands[i], "r");
        if (fileRead == NULL) {
            perror("file could not be read...\n");
        } else {
            fileExistsCount += 1;
        }
    }
    
    if (fileExistsCount != filesExpectedToSend) {
        printf("Unable to find one or more of the input files.\n");
        return;
    }
    
    // Handshake
    sendToServer(ConnectSocket, inputCopy, inputSize);
    char recvbuf[BUFLEN] = {0,};
    int iResult = 0;
    printf("\n--- Response --- \n%s\n", receive(ConnectSocket, iResult, recvbuf, BUFLEN));
    memset(recvbuf, 0, BUFLEN);
    receive(ConnectSocket, iResult, recvbuf, BUFLEN);
    
    // ok -- Handshake successful
    if (recvbuf[0] == 'o') {
        char fileReadBuffer[FILEBUFLEN] = {0, };
        
        for (int i = 2; i < 2 + filesExpectedToSend; i++) {
            printf("reading file: %s\n", commands[i]);
            fileRead = fopen(commands[i], "r");
            if (fileRead == NULL) {
                perror("file could not be read...\n");
            }
            fread(fileReadBuffer, FILEBUFLEN, 1, fileRead);
            sendToServer(ConnectSocket, fileReadBuffer, FILEBUFLEN);
            memset(fileReadBuffer, 0, FILEBUFLEN);
        }
        
        receive(ConnectSocket, iResult, recvbuf, BUFLEN);
        printf("\n--- Response --- \n%s\n", recvbuf);
        
    } else {
        // Error handling
        printf("\n--- Response --- \n%s\n", recvbuf);
    }
    
    return;
    
}

// Main loop
void commandLine(int ConnectSocket) {
    
    char input[BUFLEN];
    char inputCopy[BUFLEN];
    
    char recvbuf[2048];
    int iResult = 0;
    int recvbuflen = 2048;
    int k;
    printf("Enter a command: ");
    
skipProcessing:
    while (fgets(input, BUFLEN, stdin) != NULL) {
        // Send commands to the server until the user quits or an error occurs
        // Ensures the code after this does not run should the user just press enter
        // With no input
        if (input[0] == '\n') {
            printf("Enter a command: ");
            goto skipProcessing;
        }
        
        // Signal
        signal(SIGCHLD, sig_child);
        
        // Make a copy of the input to send to the server as "input"
        // is not usable after it has been split
        strcpy(inputCopy, input);

        char ** commands;
        k = 0;
        commands = separateCommands(input, &k);
        
        pid_t pid;
        memset(recvbuf, 0, 2048);
        
        // Begin processing the command
        if ((strcmp(commands[0], "quit") == 0) || (strcmp(commands[0], "-q") == 0)) {
            printf("Quitting application, disconnecting server\n");
            sendToServer(ConnectSocket, inputCopy, BUFLEN);
            close(ConnectSocket);
            exit(0);
            
        } else if (strcmp(commands[0], "put") == 0) {
            // Send the command
            put(ConnectSocket, inputCopy, BUFLEN, commands, k);
            printf("\nEnter a command: ");
            
        } else if ((strcmp(commands[0], "get") == 0)) {
            
            int done = 0;
            
            if (k == 3) {
                sendToServer(ConnectSocket, inputCopy, BUFLEN);
                char largeBuf[FILEBUFLEN];
                receive(ConnectSocket, iResult, largeBuf, FILEBUFLEN);
                
                int numLines = 0;
                for (int i = 0; i < FILEBUFLEN; i++) {
                    if ((int)largeBuf[i] == 10) {
                        numLines += 1;
                        
                        if (numLines % 40 != 0) {
                            printf("%c", largeBuf[i]);
                        } else {
                            printf("\n---");
                            getchar();
                        }
                        
                    } else if ((int)largeBuf[i] == 0) {
                        // we can stop now
                        done = 1;
                        break;
                    } else {
                        printf("%c", largeBuf[i]);
                    }
                }
                
            } else {
                printf("get takes 3 arguments");
            }
            if (done == 1) {
                printf("\nEnter a command: ");
            }
            
        } else {
            // Non-synchronous operation
            
            if ((pid = fork()) == 0) {
                
                // Just in case
                if (k == 0) {
                    perror("Unable to run command (k was unexpectedly 0)");
                    exit(-1);
                }
                
                if (strcmp(commands[0], "sys") == 0 && k != 0) {
                    sendToServer(ConnectSocket, inputCopy, BUFLEN);
                    printf("\n--- Response --- \n%s\n", receive(ConnectSocket, iResult, recvbuf, recvbuflen));
                }
                else if ((strcmp(commands[0], "list") == 0)) {
                    sendToServer(ConnectSocket, inputCopy, BUFLEN);
                    printf("\n--- Response --- \n%s\n", receive(ConnectSocket, iResult, recvbuf, recvbuflen));
                }
                else if (strcmp(commands[0], "run") == 0) {
                    
                    int shouldLocal = 0;
                    for (int i = 0; i < k; i++) {
                        if (strcmp(commands[i], "-f") == 0) {
                            shouldLocal = i+1;
                            break;
                        }
                    }
                    
                    char fileName[BUFLEN] = "";
                    if (shouldLocal != 0) {
                        strcat(fileName, commands[shouldLocal]);
                        strcat(fileName, ".txt");
                    }
                    
                    if (access(fileName, F_OK) == 0) {
                        printf("File exists!\n");
                    } else {
                        sendToServer(ConnectSocket, inputCopy, BUFLEN);
                        
                        printf("\n--- Response --- \n%s\n", receive(ConnectSocket, iResult, recvbuf, recvbuflen));
                        
                        if (shouldLocal != 0) {
                            FILE *fp;
                            
                            fp = fopen(fileName, "w+");
                            fwrite(recvbuf, 1, recvbuflen, fp);
                            fclose(fp);
                        }
                    }
                    
                }
                else {
                    printf("Command is malformed or not accepted.\nPlease use the following:\n* put progname sourcefile[s] [-f]\n* get progname sourcefile\n* list [-l] progname\n* sys\n");
                }
                    
                printf("\nEnter a command: ");
                    
            } else if (pid < 0) {
                perror("Child process creation with fork failed with error");
            }
            
        }
        
    }
    
    close(ConnectSocket);
    exit(0);
}


// Connects socket and handles errors
int connectServer(const char *serverAddress) {

    int iResult;
    struct sockaddr_in Address = {0};
    int ConnectSocket = 0;

    // Set Address struct elements
    Address.sin_family = AF_INET; // IPv4
    Address.sin_addr.s_addr = inet_addr(serverAddress); // Address to connect to server
    Address.sin_port = htons( PORT );
    
    ConnectSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (ConnectSocket < 0) {
        perror("Socket failed with error: \n");
        _exit(1);
    }
    
    // Connect to server
    iResult = connect(ConnectSocket, (struct sockaddr *)&Address, sizeof(Address));
    if (iResult < 0) {
        close(ConnectSocket);
        ConnectSocket = -1;
    }
    
    if (ConnectSocket < 0 ) {
        perror("Unable to connect to server\n");
        _exit(1);
    }

    printf("Connected to Server...\n");
    return ConnectSocket;
    
}


int main(int argc, const char * argv[]) {
    // Check to make sure we have enough args
    if (argc != 2) {
        printf("usage: %s server-ip\n", argv[0]);
        return 1;
    }
    
    //Connect to the server
    int ConnectSocket;
    ConnectSocket = connectServer(argv[1]);
    
    // Main loop
    commandLine(ConnectSocket);
    
    return 0;
}
