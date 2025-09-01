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
#define INITIAL_ROOM_SIZE 5
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

/*
 * Handle incoming connections. 
 */
void handle_new_connection(int listener, struct pollfd **pfds, int *fd_count, int *fd_size) {
    struct sockaddr_storage remoteAddr;
    socklen_t addrlen;
    int newfd;
    char remoteIP[INET6_ADDRSTRLEN];
    
    addrlen = sizeof(remoteAddr);
    newfd = accept(listener, (struct sockaddr *) &remoteAddr, &addrlen);

    if (newfd == -1) {
        perror("server: accept");
    } else {
        add_to_pfds(pfds, newfd, fd_count, fd_size);
        printf("pollserver: new connection from %s on socket %d\n", inet_ntop2(&remoteAddr, remoteIP, sizeof(remoteIP)), newfd);
    }
}

/*
 * Handle regular client data or client hangups.
 */
void handle_client_data(int listener, int *fd_count, struct pollfd *pfds, int *pfd_i) {
    char buffer[MAX_DATA_SIZE];
    int sender_fd = pfds[*pfd_i].fd;

    int nbytes = recv(sender_fd, buffer, sizeof(buffer), 0);

    if (nbytes <= 0) {
        if (nbytes == 0) {
            //Connection closed.
            printf("pollserver: socket %d hung up\n", sender_fd);
        } else {
            perror("pollserver: recv");
        }

        close(sender_fd);
        del_from_pfds(pfds, *pfd_i, fd_count);

        (*pfd_i)--; //Reexamine slot we just deleted from.
    } else {
        printf("pollserver: recv from fd %d: %.*s", sender_fd, nbytes, buffer);

        //Send to everyone:
        for (int j = 0; j < fd_count; j++) {
            int dest_fd = pfds[j].fd;

            if (dest_fd != listener && dest_fd != sender_fd) {
                if (send(dest_fd, buffer, nbytes, 0) == -1) {
                    fprintf(stderr, "server: send");
                }
            }
        }
    }
}

/*
 * Process all existing connections.
 */
void process_connections(int listener, int *fd_count, int *fd_size, struct pollfd **pfds) {
    for (int i = 0; i < fd_count; i++) {
        if ((*pfds)[i].revents & (POLLIN | POLLHUP)) {
            if ((*pfds)[i].fd == listener) {
                handle_new_connection(listener, pfds, fd_count, fd_size);
            } else {
                handle_client_data(listener, fd_count, pfds, &i);
            }
        }
    }
}

/*
 * Main: create a listener and connection set, loop forever processing connections.
 */
int main(void) {
    int listener;
    int fd_count = 0; 
    int fd_size = INITIAL_ROOM_SIZE; 
    struct pollfd* pfds = (struct pollfd *) malloc(sizeof(struct pollfd) * fd_size);
    
    listener = get_listener_socket();

    if (listener == -1) {
        fprintf(stderr, "error getting listening socket.");
        exit(1);
    }

    pfds[0].fd = listener;
    pfds[0].events = POLLIN;

    fd_count = 1;

    puts("pollserver: waiting for connections...");

    while(1) {
        int poll_count = poll(pfds, fd_count, -1);

        if (poll_count == -1) {
            perror("pollserver: poll");
            exit(1);
        }
        
        process_connections(listener, &fd_count, &fd_size, &pfds);
    }

    free(pfds);
}