CC=gcc

all: server client
.PHONY: all

server:
	$(CC) -o server servermain.c;

client:
	$(CC) -o client clientmain.c;