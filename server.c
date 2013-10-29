/*
 * Server implementation.
 *
 * Copyright Marko Karjalainen, 2013
 */

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
    int initialized;
    pthread_t thread;
    int sockfd;
    char *user;
}client; 


pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t max_condition = PTHREAD_COND_INITIALIZER;
pthread_cond_t client_initialized = PTHREAD_COND_INITIALIZER;
int threadcount;
int exitVal;
client clients[MAX_CLIENT_COUNT];

void error(const char *msg)
{
    perror(msg);
    pthread_cond_destroy(&max_condition);
    pthread_mutex_destroy(&client_mutex);
    pthread_exit((void*)1);
}

int getusercount(void)
{
    int i, count;
    
    count = 0;
    pthread_mutex_lock(&client_mutex);
    
    for (i = 0; i < MAX_CLIENT_COUNT; ++i)
    {
        if (clients[i].initialized == 1)
            ++count;
    }
    pthread_mutex_unlock(&client_mutex);
    return count;
}

void sendusercount(int socket)
{
    int n, loggedUsers;
    char userCount[8];
    char message[256];
    
    loggedUsers = getusercount();
    sprintf(userCount, "%d", loggedUsers);
    strcpy(message, userCount);
    strcat(message, " Users logged in.");
    n = write(socket, message, strlen(message));
    if (n < 0)
        error("ERROR in write");
}
    
void removeclient(int socket)
{
    int i;
    pthread_mutex_lock(&client_mutex);

    if (threadcount > 0)
    {
        threadcount--;
    }
    for (i = 0; i < MAX_CLIENT_COUNT; ++i)
    {
        if (clients[i].initialized == 1 && clients[i].sockfd == socket)
        {
            clients[i].initialized = 0;
            clients[i].sockfd = 0;
            close(socket);
            if (clients[i].user != NULL)
            {
                printf("Remove client %s\n", clients[i].user);
                free(clients[i].user);
            }
            clients[i].user = NULL;
            break;
        }
    }    
    pthread_cond_signal(&max_condition);
    pthread_mutex_unlock(&client_mutex);
}

void thread_error(int fd, const char *msg)
{
    printf("thread error: %s\n", msg);
    perror(msg);
    removeclient(fd);
    pthread_exit((void*)1);
}

void addclientthread(int socket, pthread_t *thread)
{
    int i;
    pthread_mutex_lock(&client_mutex);
    
    for (i = 0; i < MAX_CLIENT_COUNT; ++i)
    {
        if (clients[i].initialized == 1  && clients[i].sockfd == socket)
        {
            clients[i].thread = *thread;
            break;
        }    
    }
    pthread_mutex_unlock(&client_mutex);
}

void addclient(int socket)
{
    int i, n;
    pthread_mutex_lock(&client_mutex);
    threadcount++;
    if (threadcount > MAX_CLIENT_COUNT)
    {
        n = write(socket, SERVER_FULL, SERVER_FULL_LEN);
        if (n < 0)
        {
            pthread_mutex_unlock(&client_mutex);
            thread_error(socket, "ERROR in write");
        }
        printf("Waiting for other client handler threads to finish\n");       
        pthread_cond_wait(&max_condition, &client_mutex);
        printf("Waiting for other threads done\n");
    }
    for (i = 0; i < MAX_CLIENT_COUNT; ++i)
    {
        if (clients[i].initialized == 0)
        {
            clients[i].initialized = 1;
            clients[i].sockfd = socket;
            clients[i].user = NULL;
            break;
        }
    }
    printf("add client: client count %d\n", threadcount);
    pthread_mutex_unlock(&client_mutex);
    
}

void sendmessagefrom(int socket, const char* message)
{
    int i, n;
    char buffer[512];
    
    if (message == NULL)
    {
        printf("send message NULL!\n");
        return;
    }
    pthread_mutex_lock(&client_mutex);
    for (i = 0; i < MAX_CLIENT_COUNT; ++i)
    {
        if (clients[i].initialized == 1 && clients[i].sockfd == socket)
        {
            memset(buffer, 0, 512);
            strcat(buffer, "[");
            if (clients[i].user != NULL)
                strcat(buffer, clients[i].user);
            strcat(buffer, "]");
            strcat(buffer, message);
        }
    }

    if (strlen(buffer) > 0)
    {
        for (i = 0; i < MAX_CLIENT_COUNT; ++i)
        {
            if (clients[i].initialized == 1 && clients[i].sockfd != socket)
            {
                n = write(clients[i].sockfd, buffer, strlen(buffer));
                if (n < 0)
                {
                    pthread_mutex_unlock(&client_mutex);
                    thread_error(clients[i].sockfd, "ERROR writing to socket");
                }
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
        if (clients[i].initialized == 1 && clients[i].sockfd == socket)
        {
            if (clients[i].user != NULL)
                free(clients[i].user);

            clients[i].user = malloc(strlen(nick));
            if (clients[i].user == NULL)
            {
                pthread_mutex_unlock(&client_mutex);
                thread_error(socket, "Out of memory");
            }
            printf("Add user %s\n", nick);
            strcpy(clients[i].user, nick);
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
    
}

void* handle_client(void* fd)
{
    int n;
    int oldtype;
    int clientfd;
    int nickLen;
    char buffer[256];
    char *token;
    char usernick[256];
    clientfd = (*(int*)fd);     
    nickLen = 0;
    n = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldtype);
    if (n < 0)
        thread_error(clientfd, "ERROR in setcanceltype");

    addclientthread(clientfd, (pthread_t*)pthread_self());
    sendusercount(clientfd);
    
    while (1)
    {
        printf("handle client %d\n", clientfd);
        memset(buffer, 0, 256);
        n = read(clientfd, buffer, 255);
        if (n <= 0) 
        {
            thread_error(clientfd, "Client disconnected");
        }
        if (nickLen == 0)
        {
            token = strtok(buffer, " ");
            if (token == NULL)
                thread_error(clientfd, "ERROR client protocol");
            if (strcmp(token, USER) == 0)
            {
                token = strtok(NULL, " ");
                if (token == NULL)
                    thread_error(clientfd, "ERROR reading client message");
                
                nickLen = strlen(token);
                if (nickLen > 0 && nickLen <= 256)
                {
                    strcpy(usernick, (const char*)token);
                    addusernick(clientfd, (const char*)&usernick);
                    
                    n = write(clientfd, WELCOME, WELCOME_LEN);
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
        }
        else
        {
            sendmessagefrom(clientfd, buffer);
        }
     }
     removeclient(clientfd);
     pthread_exit(NULL);
 }

int main(int argc, char *argv[])
{
     int sockfd, acceptfd, portno, i;
     socklen_t clilen;
     int sockoption = 1;
     struct client newclient = { .initialized = 0, .sockfd = 0, .user = NULL };
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
        addclient(acceptfd);
        if ((thread_ret = pthread_create(&newclient.thread, NULL, handle_client, (void*)&acceptfd)))
        {
            perror("Error creating thread");
            close(acceptfd);
        }
     }
     printf("Exiting server\n");
     for (i = 0; i < MAX_CLIENT_COUNT; ++i)
     {
        if (clients[i].user != NULL)
        {
            free(clients[i].user);
            clients[i].user = NULL;
        }
        pthread_join(clients[i].thread, NULL);
     } 
     close(sockfd);
     pthread_cond_destroy(&max_condition);
     pthread_mutex_destroy(&client_mutex);
     pthread_exit(NULL);
}



