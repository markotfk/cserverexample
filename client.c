/*
 * Client implementation.
 *
 * Copyright Marko Karjalainen, 2013
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <pthread.h>

#include "constants.h"

pthread_t thread;
int readerRunning = 0;

void error(const char *msg)
{
    perror(msg);
    pthread_exit((void*)1);
}

void *messageReader(void *sockfd)
{
    int socket, n;
    char buffer[256];
    
    socket = (*(int*)sockfd);
    while (readerRunning == 1)
    {
        memset(buffer, 0, 256);
        n = read(socket, buffer, 255);
        if (n <= 0)
        {
            close(socket);
            error("Connection closed");
        }
        printf("\n%s", buffer);
    }
    pthread_exit(NULL);
}

void closeReader(int socket)
{
    close(socket);
    readerRunning = 0;
    pthread_join(thread, NULL);
}
    
    
int main(int argc, char *argv[])
{
    int sockfd, portno, n, thread_ret;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char nick[256];
    char *userRegister;
    char buffer[256];
    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       exit(0);
    }
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    server = gethostbyname(argv[1]);
    if (server == NULL) 
    {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    do {
        printf("Enter your nick name: ");
        memset(nick, 0, 256);
        fgets(nick, 255, stdin);
    } 
    while (strlen(nick) <= 1);
    
    if (nick[strlen(nick)-1] == '\n')
        nick[strlen(nick)-1] = '\0';
        
    userRegister = malloc(USERCLIENT_LEN + strlen(nick));
    if (userRegister == NULL)
        error("Out of memory");
    strcpy(userRegister, USERCLIENT);
    strcat(userRegister, (const char*)&nick);
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");

    n = write(sockfd, userRegister, strlen(userRegister));
    if (n < 0)
        error("ERROR write");

    memset(buffer, 0, 256);
    printf("Connected.\n");
    readerRunning = 1;
    
    if ((thread_ret = pthread_create(&thread, NULL, messageReader, (void*)&sockfd)))
    {
        close(sockfd);
        error("Error creating thread");
    }

    while (strcmp(ERROR, buffer) != 0)
    {
        memset(buffer, 0, 256);
        printf("\n[%s]", nick);
        fgets(buffer, 255, stdin);
        n = write(sockfd, buffer, strlen(buffer));
        if (n < 0) 
        {
            closeReader(sockfd);
            error("ERROR writing to socket");
        }
    }
    printf("server: %s, closing...\n", buffer);
    closeReader(sockfd);
    printf("Bye\n");
    pthread_exit(NULL);
}

