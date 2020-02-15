/*
** pollserver.c -- a cheezy multiperson chat server
** when a connection is ready-to-read, read data from it and send that data to all the other connections
**
** @test: run it in one window, then telnet localhost 9034 from a number of other windows
**        you should be able to see what you typed in one terminal appear in the other ones
**        to exit, hit "CTRL-]"" and type quit
*/

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

void* getInAddr(struct sockaddr* sa) {
    return (sa->sa_family == AF_INET)
    ? &(((struct sockaddr_in*)sa)->sin_addr)
    : &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int setListener() {
    int listener;
    int yes = 1;
    int status;
    struct addrinfo hints, *servinfo, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;  // return instead of exit(1) because caller will handle errors
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }

        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            perror("setsockopt");
            return -1;
        }

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;  // bind to the first local address available
    }

    if (p == NULL) {
        fprintf(stderr, "server: unable to bind an available address\n");
        return -1;
    }

    freeaddrinfo(servinfo);

    if (listen(listener, BACKLOG) == -1) {
        return -1;
    }

    return listener;
}

void addToPfds(struct pollfd* pfds[], int new_fd, int* fd_count, int* fd_size) {
    if (*fd_count == *fd_size) {
        *fd_size *= 2;  // if room used up, double the size
        *pfds = realloc(*pfds, (*fd_size) * sizeof(**pfds));
    }

    (*pfds)[*fd_count].fd = new_fd;
    (*pfds)[*fd_count].events = POLLIN;
    (*fd_count)++;
}

void delFrPfds(struct pollfd pfds[], int i, int* fd_count) {
    pfds[i] = pfds[(*fd_count) - 1];  // copy the one from the end over this one
    (*fd_count)--;
}

int main(void) {
    int listener, new_fd;
    struct sockaddr_storage cli_addr;  // it's auto, don't need to initialize or manipulate memory
    socklen_t sin_size;
    char buf[256];  // buffer for client data
    char ipstr[INET6_ADDRSTRLEN];

    // start off with room for 5 connections and then realloc as necessary
    // although we never have a chance to free memory, as for a chat room, this is reasonable
    // because it is supposed to run forever (or crash, in which case memory is freed by OS)
    // struct pollfd pfds[fixed_size] can also be used in some cases, but boundary check is needed
    int fd_count = 0, fd_size = 5;
    struct pollfd* pfds = malloc(fd_size * sizeof(*pfds));

    // establish the listner
    listener = setListener();
    if (listener == -1) {
        fprintf(stderr, "unable to establish a listner socket\n");
        exit(1);
    }

    // add the listener to pfds[] to monitor data and events
    pfds[0].fd = listener;
    pfds[0].events = POLLIN;  // let me know when data is ready to recv() from a client connection
    fd_count = 1;  // at first, only 1 socket (the listner) to monitor

    // main loop
    while (1) {
        int poll_count = poll(pfds, fd_count, -1);  // timeout < 0, infinite monitor
        if (poll_count == -1) {
            perror("poll");
            exit(1);
        }

        for (int i = 0; i < fd_count; i++) {
            if (pfds[i].revents & POLLIN) {  // event occured, data is ready to recv() from pfds[i]

                if (pfds[i].fd == listener) {  // recv() from listener itself, means an incoming client connection
                    sin_size = sizeof(cli_addr);
                    new_fd = accept(listener, (struct sockaddr*)&cli_addr, &sin_size);

                    if (new_fd == -1) {
                        perror("accept");
                    } else {
                        addToPfds(&pfds, new_fd, &fd_count, &fd_size);
                        printf("pollserver: new connection from %s on socket %d\n",
                               inet_ntop(cli_addr.ss_family, getInAddr((struct sockaddr*)&cli_addr), ipstr, INET6_ADDRSTRLEN),
                               new_fd
                              );
                    }
                } else {  // recv() from a client, means a client in pfds[] has sent data
                    int sender = pfds[i].fd;
                    int nbytes = recv(sender, buf, sizeof(buf), 0);

                    if (nbytes <= 0) {  // recv() failed
                        if (nbytes == 0) {  // connection closed by client
                            printf("pollserver: socket %d has quit\n", sender);
                        } else {
                            perror("recv");
                        }
                        close(sender);
                        delFrPfds(pfds, i, &fd_count);

                    } else {  // data received from client, then send to everyone except myself and the sender
                        for(int j = 0; j < fd_count; j++) {
                            int receiver = pfds[j].fd;
                            if (receiver != listener && receiver != sender) {
                                if (send(receiver, buf, nbytes, 0) == -1) {
                                    perror("send");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return EXIT_SUCCESS;
}
