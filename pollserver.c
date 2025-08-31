#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#define PORT "9034"
#define BACKLOG 10
#define INITIAL_ROOM_SIZE 4
#define MAX_DATA_SIZE 256

/*
 * Convert socket to IP address string.
 * addr: struct sockaddr_in or struct sockaddr_in6
 */
const char *inet_ntop2(void *addr, char *buffer, size_t size) {
    struct sockaddr_storage *sas = addr;
    struct sockaddr_in *sa4;
    struct sockaddr_in6 *sa6;
    void *src;

    switch (sas->ss_family) {
        case AF_INET:
            sa4 = addr;
            src = &(sa4->sin_addr);
            break;
        case AF_INET6:
            sa6 = addr;
            src = &(sa6->sin6_addr);
            break;
        default:
            return NULL;
    }

    return inet_ntop(sas->ss_family, src, buffer, size);
}

/*
 * Return a listening socket FD.
 */
int get_listener_socket(void) {
    int listener; 
    int yes = 1;
    int rv;

    struct addrinfo hints, *ai, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "pollserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener > 0) {
            continue;
        }

        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    if (p == NULL) {
        return -1;
    }

    freeaddrinfo(ai);

    if (listen(listener, BACKLOG) == -1) {
        return -1;
    }

    return listener;
}

/* 
 * Add a new file descriptor to the set.
 */
void add_to_pfds(struct pollfd **pfds, int newfd, int *fd_count, int *fd_size) {
    if (*fd_count == *fd_size) {
        *fd_size *= 2;
        *pfds = realloc(*pfds, sizeof(**pfds) * (*fd_size));
    }

    int index = (*fd_count)++;
    (*pfds)[index].fd = newfd;
    (*pfds)[index].events = POLLIN; //Check ready-to-read
    (*pfds)[index].revents = 0;
}

/*
 * Remove a file descriptor at a given index from the set.
 */
void del_from_pfds(struct pollfd pfds[], int i, int *fd_count) {
    if (i < 0 || i > *fd_count - 1) {
        return;
    }

    pfds[i] = pfds[*fd_count - 1];
    *fd_count--;
}

void handle_incoming_connections(int sockfd, struct pollfd pfds[], int *fd_count, int *fd_size) {
    struct sockaddr_storage theirAddr;
    socklen_t sin_size;
    char buffer[INET6_ADDRSTRLEN];
    int newfd = accept(sockfd, (struct sockaddr *) &theirAddr, &sin_size);

    if (newfd < 0) {
        perror("server: accept");
        return;
    }

    add_to_pfds(&pfds, newfd, fd_count, fd_size);
    printf("server: connection from %s\n", inet_ntop2(&theirAddr, buffer, sizeof(buffer)));
}

void broadcast_chat_message(struct pollfd pfds[], int fd_count, int sender_fd, char *msg) {
    for (int i = 0; i < fd_count; i++) {
        struct pollfd pfd = pfds[i];
        if (pfd.fd == sender_fd) {
            continue;
        }
        if (send(pfd.fd, msg, strlen(msg), 0) == -1) {
            fprintf(stderr, "server: send");
        }
    }
}

int main(void) {
    int sockfd = get_listener_socket();
    int fd_count = 0, fd_size = INITIAL_ROOM_SIZE, numEvents = 0;
    struct pollfd* pfds = (struct pollfd *) malloc(sizeof(struct pollfd) * fd_size);
    char buffer[MAX_DATA_SIZE];
    ssize_t received;

    printf("server is listening for connections on port %s\n", PORT);

    while(1) {
        handle_incoming_connections(sockfd, pfds, fd_count, fd_size);
        numEvents = poll(pfds, fd_count, 1000);

        if (numEvents > 0) {
            for (int i = 0; i < fd_count; i++) {
                int to_read = pfds[i].revents & POLLIN;
                int fd = pfds[i].fd;

                if (to_read) {
                    received = recv(fd, buffer, MAX_DATA_SIZE - 1, 0);
                    if (read == -1) {
                        fprintf(stderr, "server: read");
                    } else {
                        buffer[received] = '\0';
                        broadcast_chat_message(pfds, fd_count, fd, buffer);    
                    }
                }
            }
        }
    }
}