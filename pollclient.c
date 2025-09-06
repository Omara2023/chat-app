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
#define HOST "localhost"
#define MAX_MESSAGE_SIZE 256

void init_hints(struct addrinfo *hints) {
    memset(hints, 0, sizeof(struct addrinfo));
    hints->ai_family = AF_UNSPEC;
    hints->ai_socktype = SOCK_STREAM;
}

int get_server_socket() {
    struct addrinfo hints, *res, *p;
    int sockfd, rv;
    char s[INET6_ADDRSTRLEN];

    init_hints(&hints);
    if ((rv = getaddrinfo(HOST, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if (sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol) < 0) {
            perror("pollclient: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("pollclient: connect");
            close(sockfd);
            continue;
        }
        
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "pollclient: failed to connect\n");
        return -1;
    }

    freeaddrinfo(res);
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *) p->ai_addr), s, sizeof(s));
    printf("pollclient: connected to %s\n", s);
    return sockfd;
}

void handle_event(int server_socket, struct pollfd *pfds, int i) {
    int offending_fd = pfds[i].fd;
    char buffer[MAX_MESSAGE_SIZE];
    uint32_t nbytes, to_receive;

    if (offending_fd == STDIN_FILENO) { //stdin so the user has typed something.
        nbytes = read(STDIN_FILENO, buffer, sizeof(buffer));    
        if (nbytes > 0) {
            //prefix number of bytes to send.
            if (send(server_socket, &nbytes, sizeof(nbytes), 0)) {
                perror("pollclient: send");
            }

            if (send(server_socket, buffer, nbytes, 0) == -1) {
                perror("pollclient: send");
            }
        }
    } else { //assuming must be from server. 
        nbytes = recv(server_socket, &to_receive, sizeof(to_receive), 0);
        if (nbytes <= 0) {
            if (nbytes == 0) {
                printf("lost connection with server RIP.\n");
                exit(1);
            }
        } else {
            //should we be checking if to_receive is 0 too? aka a valid message prefix came of agreed length but represents number 0. what does this mean/when could this situation arise? blank msg sent?
            nbytes = 0;
            while (nbytes < to_receive) {
                nbytes += recv(server_socket, buffer + nbytes, sizeof(buffer) - nbytes, 0);
            }
            printf("%s", buffer); //should this be null terminated? can we assume the server sent something null terminated? aka client 1 sends, is that NT? how does server?
        }
    }
}

void process_connections(int server_socket, struct pollfd *pfds, int fd_count) {
    for (int i = 0; i < fd_count; i++) {
        if (pfds[i].revents & (POLLIN | POLLHUP)) {
            handle_event(pfds, fd_count, i);
        } 
    }
}

int main(void) {
    int server_fd = get_connected_socket();
    struct pollfd pfds[2];
    pfds[0].fd = server_fd;
    pfds[0].events = POLLIN | POLLHUP;

    pfds[1].fd = STDIN_FILENO;
    pfds[1].events = POLLIN;

    while (1) {
        int poll_count = poll(pfds, 2, -1);

        if (poll_count == -1) {
            perror("pollclient: poll");
            exit(1);
        }

        process_connections(server_fd, pfds, 2);
    }
}