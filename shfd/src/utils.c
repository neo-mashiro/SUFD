/*
** utils.c -- some useful network wrapper and utility functions
**
** @reference: CS 464/564 by Dr. Stefan Bruda - https://cs.ubishops.ca/home/cs464
**             Beej's Guide to Network Programming - http://beej.us/guide/bgnet/html/
**             Beej's Guide to Unix IPC - http://beej.us/guide/bgipc/html/single/bgipc.html
*/

#include "utils.h"

/* error code */
const int err_sock    = -2;
const int err_addr    = -1;
const int err_connect = -3;
const int err_proto   = -4;
const int err_bind    = -5;
const int err_listen  = -6;
const int err_opt     = -7;
const int err_sig     = -8;
const int err_poll    = -9;
const int err_send    = -10;
const int err_recv    = -11;

int checkDigit(const char* str) {
    char buf[strlen(str) + 1];
    strcpy(buf, str);
    int len = strlen(buf);
    if (buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    for (int i = 0; i < len; i++) {
        if (!isdigit(buf[i])) { return 0; }
    }
    return 1;
}

char* convertPort(unsigned short port) {
    static char port_str[10];
    sprintf(port_str,"%d", port);
    return port_str;
}

void* getInAddr(struct sockaddr* sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    } else {
        return &(((struct sockaddr_in6 *) sa)->sin6_addr);
    }
}

int sendall(int fd, const char* buf, int* len) {
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n = 0;

    while (total < *len) {
        n = send(fd, buf + total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }

    *len = total;  // return number of bytes actually sent in *len
    return (n == -1 ? -1 : 0);  // return -1 on failure, 0 on success
}

int recvtimeout(int sd, char* buf, int len, int timeout) {
    struct pollfd pfds;
    pfds.fd = sd;
    pfds.events = POLLIN;

    int n = poll(&pfds, 1, timeout);
    if (n == 0) { return -2; }   // timeout
    if (n == -1) { return -1; }  // error

    return recv(sd, buf, len, 0);  // return # of bytes received, or 0 if connection closed on the other end
}

int readtimeout(int sd, char* buf, int len, int timeout) {
    struct pollfd pfds;
    pfds.fd = sd;
    pfds.events = POLLIN;

    int n = poll(&pfds, 1, timeout);
    if (n == 0) { return -2; }   // timeout
    if (n == -1) { return -1; }  // error

    return read(sd, buf, len);  // return # of bytes received
}

int socketConnect(const char* host, const char* port) {
    int status, sockfd;
    struct addrinfo hints, *servinfo, *p;
    char ipstr[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return err_addr;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) < 0) {
            perror("client: connect");
            close(sockfd);
            continue;
        }

        break;  // connect to the first reachable address we can find
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return err_connect;
    }

    inet_ntop(p->ai_family, getInAddr(p->ai_addr), ipstr, sizeof(ipstr));
    printf("client: connected to %s\n", ipstr);

    freeaddrinfo(servinfo);
    return sockfd;
}

int socketTalk(int sockfd, char* req, int timeout, char* host) {
    if (send(sockfd, req, strlen(req), 0) < 0) {
        perror("send");
        return err_send;
    }

    struct pollfd pfds[1];
    pfds[0].fd = sockfd;
    pfds[0].events = POLLIN;
    int n_res;

    while ((n_res = poll(pfds, 1, timeout)) != 0) {  // timeout in milliseconds
        char res[256];  // server response
        memset(res, 0, sizeof(res));
        int n_bytes = 0;  // number of bytes received

        if (n_res < 0) {
            perror("poll");
            return err_poll;
        }

        if (pfds[0].revents & POLLIN) {
            if ((n_bytes = recv(sockfd, res, sizeof(res) - 1, 0)) <= 0) {
                if (n_bytes == 0) {  // when server shutdown it sends FIN to us
                    printf("\nclient: connection closed by %s\n", host);
                    return 0;  // return control and close socket in main()
                } else {
                    perror("recv");
                    return err_recv;
                }
            }
        }

        res[n_bytes] = '\0';

        // convert line terminator \r\n to \n
        for (int i = 0; i < n_bytes; i++) {
            if (res[i] == '\r' && res[i + 1] == '\n') {
                for (int j = i; j < n_bytes; j++) {
                    res[j] = res[j + 1];
                }
            }
        }
        printf("%s", res);
        fflush(stdout);  // print server response in real-time (asap)
    }
    // poll timeout is reached, no more data to recv, talk is over
    printf("\n");
    return 0;
}

int socketClose(int* sockfd) {
    if (*sockfd != -1) {
        if (close(*sockfd) < 0) {
            return -1;
        }
        *sockfd = -1;
    }
    return 0;
}

int setListener(const char* host, const char* port, int backlog) {
    int listener;
    int yes = 1;
    int status;
    struct addrinfo hints, *servinfo, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (!host) {
        hints.ai_flags = AI_PASSIVE;  // bind listener to INADDR_ANY
    }

    if ((status = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return err_addr;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;  // loop until find an available local address to bind
        }

        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            perror("setsockopt");
            return err_opt;
        }

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;  // loop until find an available local address to bind
        }
        break;  // bind the first local address we can find
    }

    if (p == NULL) {
        fprintf(stderr, "server: unable to bind an available listener\n");
        return err_bind;
    }

    freeaddrinfo(servinfo);

    if (listen(listener, backlog) < 0) {
        close(listener);
        return err_listen;
    }

    return listener;
}

int readLine(int file, char* buf, size_t size) {
    size_t i;
    int begin = 1;

    for (i = 0; i < size; i++) {
        char tmp;
        int what = read(file, &tmp, 1);

        if (what == -1) { return -1; }

        if (begin) {
            if (what == 0) { return -2; }
            begin = 0;
        }

        if (what == 0 || tmp == '\n') {
            buf[i] = '\0';
            return i;
        }
        // if (!tmp) {
        //     return recv_nodata;
        // }
        buf[i] = tmp;
    }

    buf[i] = '\0';
    return i;
}

size_t tokenize(char* str, char** tokens, size_t n) {
    size_t tok_size = 1;
    tokens[0] = str;

    size_t i = 0;
    while (i < n) {
        if (str[i] == ' ') {
            str[i] = '\0';
            i++;
            for (; i < n && str[i] == ' '; i++) {}
            if (i < n) {
                tokens[tok_size] = str + i;
                tok_size++;
            }
        } else {
            i++;
        }
    }
    return tok_size;
}
