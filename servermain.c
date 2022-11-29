//
//  main.c
//  server
//
//  Created by Dante Mattson on 4/8/20.
//  Copyright © 2020 Dante Mattson. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdbool.h>
#include <regex.h>
// Mac only
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#include <signal.h>
#include <stdint.h>
#include <limits.h>

#define PORT 8080
#define BUFLEN 512
#define FILEBUFLEN 40960

// Start up the server socket and wait for connections
int serverStartup(struct sockaddr_in Address) {
    int ListenSocket;
    int iResult;

    // Initialise ListenSocket with ipv4 and stream protocol
    ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (ListenSocket < 0) {
        perror("Socket failed with error");
        exit(1);
    }
    
    // Bind to the specified address from the Address sockaddr_in struct
    iResult = bind(ListenSocket, (struct sockaddr *)&Address, sizeof(Address));
    
    if (iResult < 0) {
        perror("Bind failed with error");
        close(ListenSocket);
        exit(1);
    }
    /*
        Call listen function with the ListenSocket socket and set the max number of
        clients to a value that the server computer can reasonably handle.
    */
    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult < 0) {
        perror("Listen Failed with error:");
        close(ListenSocket);
        exit(1);
    }

    printf("Listening on port %d\n", PORT);
    printf("Waiting on connections...\n");

    return ListenSocket;
}

// Signal child to terminate zombies
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

// Calculates time difference, returns result in milliseconds
long calcTDiff(struct timespec start) {
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    return ((end.tv_nsec - start.tv_nsec)/1000000) + ((end.tv_sec - start.tv_sec)*1000);
}

// Sends to client and handles errors
void send_to_client(int ClientSocket, char *buffer, int buflen) {
    int iSendResult = (int) send(ClientSocket, buffer, buflen, 0);
    if (iSendResult < 0) {
        perror("send failed with error:");
        close(ClientSocket);
        exit(1);
    }
}

// Receives from socket and handles errors
// Writes to recvbuf
char* receive(int ClientSocket, char *recvbuf, int recvbuflen) {
    int iResult;
    iResult = (int) recv(ClientSocket, recvbuf, recvbuflen, 0);
    if (iResult > 0) {
        return recvbuf;
    }
    else if ( iResult == 0 ) {
        printf("Connection Closed\n");
        exit(1);
    }
    else if (iResult == -1) {
        perror("Receive Failed with error\n");
        exit(1);
    }
    
    return "Unable to receive\n";
}

// Runs put (to get files from client) and handles errors
void putCmd(int ClientSocket, char **commands, int noCommands) {
    struct timespec start = {0};
    char responseTime[64];
    clock_gettime(CLOCK_REALTIME, &start);
    
    // filesExpectedToRecieve - 1 for "put"
    int filesExpectedToRecieve = noCommands - 1;
    
    int shouldOverride = 0;
    if (strcmp(commands[noCommands - 1], "-f") == 0) {
        shouldOverride = 1;
        // -1 for "-f"
        filesExpectedToRecieve -= 1;
    }
    
    // Get the directory name
    char dirName[40] = {0,};
    if (noCommands >= 2) {
        strcpy(dirName, commands[1]);
    }
    
    // -1 for the dirname in the command
    filesExpectedToRecieve -= 1;
    
    // Temporary response & handshake
    char tempCommBuffer[BUFLEN] = {0, };
    sprintf(tempCommBuffer, "ok. should get %d files and put them in %s, -f:%d\n", filesExpectedToRecieve, dirName, shouldOverride);
    send_to_client(ClientSocket, tempCommBuffer, BUFLEN);
    memset(tempCommBuffer, 0, BUFLEN);
    
    // Build path for server
    char path[BUFLEN] = "";
    getcwd(path, sizeof(path));
    strcat(path, "/");
    strcat(path, dirName);
    strcat(path, "/");
    
    struct stat st = {0};
    
    // If directory doesn't exist, create it with 755 perms
    if (stat(dirName, &st) == -1) {
        mkdir(dirName, 0755);
    }
    
    // Count the number of files that can be recieved
    int totalOK = 0;
    
    char errorString[BUFLEN] = {0, };
    strcpy(errorString, "File/s ");
    
    for (int i = 2; i < 2 + filesExpectedToRecieve - shouldOverride; i++) {
        char newPath[BUFLEN] = {0, };
        strcpy(newPath, path);
        strcat(newPath, commands[i]);
        
        // if not file totalOK += 1
        FILE *tempFilePointer = fopen(newPath, "r");
        if (tempFilePointer == NULL || shouldOverride == 1) {
            totalOK += 1;
        } else {
            strcat(errorString, commands[i]);
            strcat(errorString, " ");
            fclose(tempFilePointer);
        }
        
        memset(newPath, 0, BUFLEN);
    }
    
    printf("totalOK: %d, filesExpectedToRecieve: %d\n", totalOK, filesExpectedToRecieve);
    
    if (totalOK == filesExpectedToRecieve || shouldOverride == 1) {
        sprintf(tempCommBuffer, "ok");
    } else {
        strcat(errorString, "exist in ");
        strcat(errorString, dirName);
        strcat(errorString, " on server. Use -f to override.");
        send_to_client(ClientSocket, errorString, BUFLEN);
        return;
    }
    
    send_to_client(ClientSocket, tempCommBuffer, BUFLEN);
    char *tempFileBuffer;
    tempFileBuffer = (char *) malloc(FILEBUFLEN);
    memset(tempFileBuffer, 0, FILEBUFLEN);
    int terminatedEarly = 0;
    
    // Recieve and write the files
    for (int i = 2; i < 2 + filesExpectedToRecieve; i++) {
        char newPath[BUFLEN] = {0, };
        strcpy(newPath, path);
        strcat(newPath, commands[i]);
        printf("writing %s\n", newPath);
        
        receive(ClientSocket, tempFileBuffer, FILEBUFLEN);
        
        FILE *tempFilePointer = fopen(newPath, "w+");
        if (tempFilePointer != NULL) {
            
            int fs = 0;
            for (int j = 0; j < FILEBUFLEN; j++) {
                if (tempFileBuffer[j] == 0) {
                    break;
                }
                fs += 1;
            }
            
            fwrite(tempFileBuffer, sizeof(char), fs, tempFilePointer);
            fclose(tempFilePointer);
            
        } else {
            terminatedEarly = 1;
        }
        
        memset(tempFileBuffer, 0, FILEBUFLEN);
        
    }
    
    // Error handling
    if (terminatedEarly == 0) {
        char successResponse[BUFLEN] = "File/s sent successfully!\n";
        snprintf(successResponse, 63, "\n\nTook: %lums", calcTDiff(start));
        strcat(successResponse, responseTime);
        send_to_client(ClientSocket, successResponse, BUFLEN);
        
    } else {
        send_to_client(ClientSocket, "unable to write one or more of the files!", BUFLEN);
    }
    
    // Free malloc'd memory
    free(tempFileBuffer);
    
    return;

}

// Runs the sys command using popen and returns the result
void sysCmd(int ClientSocket) {
    FILE *sys;
    struct timespec start;
    char responseTime[64];
    clock_gettime(CLOCK_REALTIME, &start);
    
    char buf[BUFLEN] = {0, };
    char line[BUFLEN] = {0, };
    
#ifdef __APPLE__
    
    sys = popen("sw_vers", "r");
    
    // Get the operating system information
    while(fgets(line, BUFLEN, sys) != NULL) {
        strcat(buf, line);
    }
    
    fclose(sys);
    
    // Read popen
    char buffer[BUFLEN];
    size_t size = sizeof(buffer);
    if (sysctlbyname("machdep.cpu.brand_string", &buffer, &size, NULL, 0) < 0) {
        perror("sysctl");
    }
    strcat(buf, "\n");
    strcat(buf, buffer);
    
#endif
    
#ifdef __linux__
    // Linux code
    sys = popen("lshw -class processor", "r");
    while (fgets(line, BUFLEN, sys) != NULL) {
        strcat(buf, line);
    }
#endif
    
    // Send response
    snprintf(responseTime, 63, "\n\nTook: %lums", calcTDiff(start));
    strcat(buf, responseTime);
    send_to_client(ClientSocket, buf, BUFLEN);
    
    return;
}

// Returns 1 = file, 0 = dir
int isFileOrDir(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

// Runs get command and handles errors
void getCmd(int ClientSocket, char **commands, int k) {
    
    struct timespec start;
    char responseTime[64];
    clock_gettime(CLOCK_REALTIME, &start);
    
    char *fileContentsBuffer = malloc(FILEBUFLEN);
    
    if (k != 3) {
        free(fileContentsBuffer);
        return;
    }
    
    char path[BUFLEN] = "";
    
    getcwd(path, sizeof(path));
    strcat(path, "/");
    strcat(path, commands[1]);
    strcat(path, "/");
    strcat(path, commands[2]);
    
    if (access(path, F_OK) != -1) {
        if (isFileOrDir(path) == 1) {
            //printf("file exists\n");
        } else {
            // do not send directories
            send_to_client(ClientSocket, "Can't send directories\n", FILEBUFLEN);
            free(fileContentsBuffer);

            return;
        }
    } else {
        //do not send anything to server if any of the files do not exist
        send_to_client(ClientSocket, "File does not exist\n", FILEBUFLEN);
        free(fileContentsBuffer);
        return;
    }
    
    FILE *fp = fopen(path, "r");
    
    // Send
    if (fp != NULL) {
        size_t newLen = fread(fileContentsBuffer, sizeof(char), FILEBUFLEN, fp);
        if (ferror(fp) != 0) {
            send_to_client(ClientSocket, "Error reading file!", FILEBUFLEN);
            fclose(fp);
            return;
        } else {
            fileContentsBuffer[newLen++] = '\0'; /* Just to be safe */
        }
        fclose(fp);
    }
    
    // Make and send response
    snprintf(responseTime, 63, "\nTook: %lums\n", calcTDiff(start));
    strcat(fileContentsBuffer, "\n");
    strcat(fileContentsBuffer, responseTime);
    
    send_to_client(ClientSocket, fileContentsBuffer, FILEBUFLEN);
    
    free(fileContentsBuffer);
    return;
}

// Runs list cmd
void listCmd(int ClientSocket, char **commands, int noCommands) {
    FILE *sys;
    struct timespec start;
    char responseTime[64];
    clock_gettime(CLOCK_REALTIME, &start);
    
    // Builds ls command to run with popen
    char cmd[40] = "ls ";
    
    if (noCommands == 1) {
        strcat(cmd, ". ");
    } else if (noCommands == 2) {
        if (strcmp(commands[1], "-l") == 0) {
            strcat(cmd, "-l ");
        }
        strcat(cmd, commands[1]);
    } else if (noCommands == 3) {
        if (strcmp(commands[1], "-l") == 0) {
            strcat(cmd, "-l ");
        }
        strcat(cmd, commands[2]);
    } else {
        strcat(cmd, commands[1]);
    }
    
    // to redirect stderr too
    strcat(cmd, " 2>&1");
    sys = popen(cmd, "r");
    
    
    // Read popen
    char buf[BUFLEN] = {0, };
    char line[BUFLEN] = {0, };
    
    while(fgets(line, BUFLEN, sys) != NULL) {
        strcat(buf, line);
    }
    
    fclose(sys);
    
    // Make and send response
    snprintf(responseTime, 63, "\nTook: %lums\n", calcTDiff(start));
    
    strcat(buf, "\n");
    strcat(buf, responseTime);
    
    send_to_client(ClientSocket, buf, BUFLEN);
    
    return;
    
}

// splits input and updates k
// input "in p ut" -> ["in", "p", "ut"]; k = 3
char** separateCommands(char * input, int* nk) {
    char *inputCopy = input;
    char delim[5] = " \n\0";
    printf("input %s\n", input);
    static char *commands[64] = {0, };

    int k = 0;
    
    commands[k] = strtok(inputCopy, delim);
    
    while( commands[k] != NULL ) {
        k++;
        commands[k] = strtok(NULL, delim);
    }
    
    printf("Commands: [");
    for (int i = 0; i < k; i++) {
        if (i == (k-1)) {
            printf("%s", commands[i]);
        } else {
            printf("%s, ", commands[i]);
        }
    }
    printf("]\n");
    
    // Assign k, basicially returning 2 params but not
    *nk = k;
    return commands;
}

// run progname args [-f localfile]
void runCmd(int ClientSocket, char **commands, int k) {
    struct timespec start;
    char responseTime[64];
    clock_gettime(CLOCK_REALTIME, &start);
    
    char returnBuffer[BUFLEN] = {0, };
    char cmd[BUFLEN] = {0, };
    
    int needsRecompile = 0;
    
    char tempDirBuffer[BUFLEN] = {0, };
    getcwd(tempDirBuffer, sizeof(tempDirBuffer));
    strcat(tempDirBuffer, "/");
    strcat(tempDirBuffer, commands[1]);
    strcat(tempDirBuffer, "/");
    
    // Error checking
    if (k > 2) {
        // used to be 2
        if (access(commands[1], F_OK) != 0) {
            send_to_client(ClientSocket, "Can't run/compile as the directory doesn't exist\n", BUFLEN);
            return;
        }
    }
    
    // Ready to run
    if (k >= 2) {
        char mainLocation[BUFLEN] = {0, };
        strcpy(mainLocation, tempDirBuffer);
        strcat(mainLocation, "main");
        int accessResult = access(mainLocation, F_OK);
        if (accessResult == 0) {
            // File exists
            // check age [ project.c -nt main ] && echo yes
            FILE *sys;
            
            char checkNtCommand[BUFLEN] = {0,};
            //strcat(checkNtCommand, "[ ");
            //strcat(checkNtCommand, commands[1]);
            strcat(checkNtCommand, "[ *.c -ot main ] && echo yes");
            
            sys = popen(checkNtCommand, "r");
            char line[BUFLEN]={0,};
            
            while(fgets(line, BUFLEN, sys) != NULL) {
                if (line[0] == 'y') {
                    printf("needs recompile -- newer source\n");
                    needsRecompile = 1;
                }
            }
            
        } else {
            //dsa
            // does not exist
            printf("determined main does not exist, accessResult: %d", accessResult);
            printf("needs recompile -- dne\n");
            needsRecompile = 1;
        }
        
        strcat(cmd, "./main ");
        for (int i = 2; i < k; i++) {
            strcat(cmd, commands[i]);
            strcat(cmd, " ");
        }
        strcat(cmd, "2>&1");
        printf("cmd: %s\n", cmd);
    }
    
    if (needsRecompile == 1) {
        chdir(tempDirBuffer);
        char compileCmd[BUFLEN] = {0, };
        // commands[1] == progname, must be named progname.c
        strcat(compileCmd, "gcc *.c -o main 2>&1 && ");
        //strcat(compileCmd, commands[1]);
        //strcat(compileCmd, ".c ");
        //strcat(compileCmd, "");
        
        // to redirect stderr too
        strcat(compileCmd, cmd);
        
        printf("compileCmd: %s\n", compileCmd);
        
        FILE *sys;
        sys = popen(compileCmd, "r");
        char line[BUFLEN]={0,};
        
        while(fgets(line, BUFLEN, sys) != NULL) {
            strcat(returnBuffer, line);
        }
        
        fclose(sys);
        chdir("..");
        
        snprintf(responseTime, 63, "\nTook: %lums\n", calcTDiff(start));
        strcat(returnBuffer, "\n");
        strcat(returnBuffer, responseTime);
        send_to_client(ClientSocket, returnBuffer, BUFLEN);
        
    } else {
        chdir(tempDirBuffer);
        FILE *sys;
        sys = popen(cmd, "r");
        char line[BUFLEN] = {0,};

        // Get the operating system information
        while(fgets(line, BUFLEN, sys) != NULL) {
            strcat(returnBuffer, line);
        }
        
        // Error checking
        if (tempDirBuffer[0] == 0) {
            printf("Empty return\n");
        }
        
        fclose(sys);
        
        // Exit directory
        chdir("..");
        
        snprintf(responseTime, 63, "\nTook: %lums\n", calcTDiff(start));
        strcat(returnBuffer, "\n");
        strcat(returnBuffer, responseTime);
        send_to_client(ClientSocket, returnBuffer, BUFLEN);
    }
    
    return;
}

void handle_request(int ClientSocket) {
    
    char recvbuf[BUFLEN];
    int iResult = -1, recvbuflen = BUFLEN;
    char ** commands;
    
    while (1) {
        
        iResult = (int) recv(ClientSocket, recvbuf, recvbuflen, 0);
        printf("recvBuf:%s", recvbuf);
        
        if (iResult > 0) {
            
            int k = 0;
            char recvbufCopy[BUFLEN];
            strcpy(recvbufCopy, recvbuf);
            commands = separateCommands(recvbuf, &k);
            
            if (strcmp(commands[0], "put") == 0) {
                printf("Running put command\n");
                putCmd(ClientSocket, commands, k);
            } else {
                
                pid_t pid;
                
                if ((pid = fork()) == 0) {
                    printf("Running new process child for query\n");
                    printf("Bytes received: %d\n", iResult);
                    printf("got from client:%s\n", recvbufCopy);
                    
                    // Compare the first command against all the expected commands names
                    if ((strcmp(commands[0], "quit") == 0) || (strcmp(commands[0], "-q") == 0)) {
                        printf("Disconnected Client \n");
                        break;
                    }
                    else if (strcmp(commands[0], "run") == 0) {
                        printf("Running run command\n");
                        runCmd(ClientSocket, commands, k);
                    }
                    else if (strcmp(commands[0], "get") == 0) {
                        getCmd(ClientSocket, commands, k);
                    }
                    else if (strcmp(commands[0], "list") == 0) {
                        // Check if a correct number of commands are passed
                        if (k <= 3 && k >= 1) {
                            printf("Running list command\n");
                            listCmd(ClientSocket, commands, k);
                            exit(0);
                        }
                        else {
                            printf("list usage: \"list [-l] directory\"\n");
                            //send_to_client(ClientSocket, "malformed list has reached the server", 38);
                            exit(1);
                        }

                    }
                    else if (strcmp(commands[0], "sys") == 0) {
                        printf("Running sys command\n");
                        sysCmd(ClientSocket);
                        exit(0);
                    }
                        
                }
                else if (pid < 0) {
                    perror("Handle request fork failed with error");
                }
                
            }
            
        }
        
        // True if the client disconnects
        else if (iResult == 0) {
            printf("Connection closing...\n");
            close(ClientSocket);
            exit(1);
        }
        
        // Error handling for recv
        else {
            perror("recv failed with error");
            close(ClientSocket);
            exit(1);
        }
        
        memset(recvbuf, 0, BUFLEN);
        signal(SIGCHLD, sig_child);
    };

}

void manageConnections(int ListenSocket) {
    int ClientSocket;

    /*
        Create a new process for each new client that is accepted and
        call the handleRequests function to place the process in an infinite loop.
        Parent will return the accept function and block until another client connects.
    */
    
    pid_t pid;
    struct sockaddr_in NewAddress;
    socklen_t addr_size = sizeof(NewAddress);
    
    while (1) {
        // Accept new clients
        ClientSocket = accept(ListenSocket, (struct sockaddr *)&NewAddress , &addr_size);
        
        if (ClientSocket < 0) {
            // Handle error where signal is caught
            if (errno == EINTR) {
                continue;
            }
            else {
                perror("Client accept failed");
                close(ClientSocket);
            }
        }
        printf("New Client Accepted from %s : %d\n", inet_ntoa(NewAddress.sin_addr), ntohs(NewAddress.sin_port));
        if ((pid = fork()) == 0) {
            close(ListenSocket);
            
            // Infinite loops that allows client to enter commands
            handle_request(ClientSocket);
            
            printf("Disconnected from %s : %d\n", inet_ntoa(NewAddress.sin_addr), ntohs(NewAddress.sin_port));
            exit(0);
        }
        else if (pid < 0) {
            perror("Fork failed with error");
        }
        close(ClientSocket);
        
    }
    
}

int main(int argc, const char * argv[]) {
    struct sockaddr_in Address = {0};

    // Specify server struct variables
    Address.sin_family = AF_INET;
    Address.sin_port = htons(PORT);
    //Address.sin_addr.s_addr = INADDR_ANY;
    inet_aton("127.0.0.1", &Address.sin_addr);
    
    int ListenSocket = serverStartup(Address);

    manageConnections(ListenSocket);
    
    return 0;
}
