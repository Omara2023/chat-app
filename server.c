#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORT "3490"
#define BACKLOG 10 // max length of pending connection queue.

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

void init_hint(struct addrinfo *hints) {
    memset(hints, 0, sizeof(*hints));
    hints->ai_family = AF_UNSPEC; //allow IPv4 or IPv6
    hints->ai_socktype = SOCK_STREAM;
    hints->ai_flags = AI_PASSIVE; //use my IP
}

int main(int argc, char** argv) {
    struct addrinfo hints, *serverInfo, *p;

    
    init_hint(&hints);
    int rv, sockfd;
    
    if ((rv = getaddrinfo(NULL, PORT, &hints, &serverInfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    //Try all results until we successfully bind
    for (p = serverInfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("server: socket");
            continue;
        }

        int yes = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("server: setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break; //success
    }

    freeaddrinfo(serverInfo);

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("server: listen");
        exit(1);
    }

    printf("server: waiting for connections on port %s...\n", PORT);

    //main accept loop (sequential)
    struct sockaddr_storage theirAddr;
    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    char *msg = "Hello, world!";
    int newFd;
    
    while (1) {
        sin_size = sizeof(theirAddr);
        newFd = accept(sockfd, (struct sockaddr *) &theirAddr, &sin_size);
        if (newFd == -1) {
            perror("server: accept");
            continue;
        }

        inet_ntop(theirAddr.ss_family, get_in_addr((struct sockaddr *) &theirAddr), s, sizeof(s));
        printf("server: got connection from %s\n", s);
            
        if (send(newFd, msg, strlen(msg), 0) == -1) {
            perror("server: send");
        }

        close(newFd);
    }

    return 0;
}

