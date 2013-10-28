#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#include "constants.h"

typedef struct client
{
    int clientid;
    pthread_t thread;
    int sockfd;
    char *user;
}client; 


pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t max_condition = PTHREAD_COND_INITIALIZER;
int threadcount;
int exitVal;
client clients[MAX_CLIENT_COUNT];

void error(const char *msg)
{
    perror(msg);
    pthread_exit((void*)1);
}

void removeclient(int socket)
{
    int i;
    pthread_mutex_lock(&client_mutex);
    printf("removeclient: Thread count %d\n", threadcount);
    if (threadcount > 0)
    {
        threadcount--;
    }
    for (i = 0; i < MAX_CLIENT_COUNT; ++i)
    {
        if (clients[i].sockfd == socket)
        {
            printf("Remove client id %d, socket %d\n", clients[i].clientid, socket);
            clients[i].clientid = 0;
            break;
        }
    }    
    pthread_cond_signal(&max_condition);
    pthread_mutex_unlock(&client_mutex);
}

void thread_error(int fd, const char *msg)
{
    perror(msg);
    removeclient(fd);
    close(fd);
    pthread_exit((void*)1);
}

void addclient(pthread_t* thread, int socket)
{
    int i;
    pthread_mutex_lock(&client_mutex);
    threadcount++;
    if (threadcount > MAX_CLIENT_COUNT)
    {
        printf("Waiting for other threads to finish\n");
        pthread_cond_wait(&max_condition, &client_mutex);
        printf("Waiting for other threads done\n");
    }
    for (i = 0; i < MAX_CLIENT_COUNT; ++i)
    {
        if (clients[i].clientid == 0)
        {
            clients[i].clientid = threadcount;
            clients[i].thread = *thread;
            clients[i].sockfd = socket;
            break;
        }
    }
    printf("addclient: Thread count %d, socket %d\n", threadcount, socket);
    pthread_mutex_unlock(&client_mutex);
    
}

void sendmessagefrom(int socket, const char* message)
{
    int i, n;
    pthread_mutex_lock(&client_mutex);
    for (i = 0; i < MAX_CLIENT_COUNT; ++i)
    {
        if (clients[i].clientid != 0 && clients[i].sockfd != socket)
        {
            n = write(clients[i].sockfd, message, strlen(message));
            if (n < 0)
            {
                thread_error(clients[i].sockfd, "ERROR writing to socket");
            }
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void addusernick(int socket, const char* nick)
{
    int i;
    pthread_mutex_lock(&client_mutex);
    for (i = 0; i < MAX_CLIENT_COUNT; ++i)
    {
        if (clients[i].clientid != 0 && clients[i].sockfd == socket)
        {
            if (clients[i].user != NULL)
            {
                free(clients[i].user);
            }

            clients[i].user = malloc(strlen(nick));
            if (clients[i].user == NULL)
            {
                thread_error(socket, "Out of memory");
            }
            strcpy(clients[i].user, nick);
        }
    }
    pthread_mutex_unlock(&client_mutex);
    
}

void* handle_client(void* fd)
{
    int n;
    int clientfd;
    int nickLen;
    char buffer[256];
    char *token;
    char usernick[256];
    clientfd = (*(int*)fd);     
    nickLen = 0;
    while (1)
    {
        memset(buffer, 0, 256);
        n = read(clientfd, buffer, 255);
        if (n < 0) 
        {
            thread_error(clientfd, "ERROR reading from socket");
        }
        token = strtok(buffer, " ");
        while (token != NULL)
        {
            if (strcmp(token, USER))
            {
                token = strtok(NULL, " ");
                nickLen = strlen(token);
                if (nickLen > 0 && nickLen <= 256)
                {
                    strcpy(usernick, (const char*)token);
                    addusernick(clientfd, &usernick);
                    
                    n = write(clientfd,WELCOME, nickLen + WELCOME_LEN);
                }
                else 
                {
                    n = write(clientfd, ERROR, ERROR_LEN);
                }
                if (n < 0) 
                {
                    thread_error(clientfd, "ERROR writing to socket");
                }
            }
            else if (nickLen > 0)
            {
                if (strcmp(token, MESSAGE))
                {
                    token = strtok(NULL, " ");
                    printf("Received message %s\n", token);
                    sendmessagefrom(clientfd, token);
                }
            
            }
            else 
            {
                n = write(clientfd, "ERROR", 5);
                if (n < 0)
                {
                    thread_error(clientfd, "ERROR writing to socket");
                }
                break;
            }
            
            
        }
     }
     removeclient(clientfd);
     close(clientfd);
     pthread_exit(NULL);
 }

int main(int argc, char *argv[])
{
     int sockfd, acceptfd, portno, i;
     socklen_t clilen;
     int sockoption = 1;
     struct client newclient;
     struct sockaddr_in serv_addr, cli_addr;
     int thread_ret;
     
     if (argc < 2) {
         error("ERROR, no port provided");
     }
     memset(&newclient, 0, sizeof(struct client));
     memset(&clients, 0, MAX_CLIENT_COUNT*sizeof(struct client));
     
     sockfd = socket(PF_INET, SOCK_STREAM, 0);
     if (sockfd < 0) 
        error("ERROR opening socket");

     setsockopt(sockfd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char*)&sockoption, sizeof(sockoption));
     memset(&serv_addr, 0, sizeof(serv_addr));
     portno = atoi(argv[1]);
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);
     if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) 
     {
         close(sockfd);
         error("ERROR on binding");
     }
     if (listen(sockfd, MAX_CLIENT_COUNT) < 0) 
     {
         close(sockfd);
         error("Error on listen");
     }
     clilen = sizeof(cli_addr);
     printf("Server listening on port %d\n", portno);
     threadcount = 0;
     exitVal = 0;
     while (exitVal == 0)
     {
        acceptfd = accept(sockfd, 
                (struct sockaddr *) &cli_addr, 
                &clilen);
        if (acceptfd < 0)
        {
            perror("ERROR on accept");
            break;
        }
        if ((thread_ret = pthread_create(&newclient.thread, NULL, handle_client, (void*)&acceptfd)))
        {
            perror("Error creating thread");
            close(acceptfd);
        }
        else 
        {
            addclient(&newclient.thread, acceptfd);
        }
     }
     printf("Exiting server\n");
     for (i = 0; i < MAX_CLIENT_COUNT; ++i)
     {
        if (clients[i].user != NULL)
        {
            free(clients[i].user);
        }
        pthread_join(clients[i].thread, NULL);
     } 
     close(sockfd);
     pthread_cond_destroy(&max_condition);
     pthread_mutex_destroy(&client_mutex);
     pthread_exit(NULL);
}



