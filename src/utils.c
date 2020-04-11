/*
** utils.c -- some useful wrapper functions
*/

#include "utils.h"

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

size_t str_split(char* str, char** tokens, size_t n) {
    size_t tok_size = 1;
    tokens[0] = str;

    size_t i = 0;
    while (i < n) {
        if (str[i] == ':') {
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

int checkDigit(const char* str) {
    char buf[strlen(str) + 1];
    strcpy(buf, str);
    int len = strlen(buf);
    if (buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    if (!isdigit(buf[0]) && buf[0] != '-') {  // first char is either digit or minus sign '-'
        return 0;
    }
    for (int i = 1; i < len; i++) {
        if (!isdigit(buf[i])) {
            return 0;
        }
    }
    return 1;
}

int findMajority(char* arr[], int n) {
    int maxCount = 0;
    int index = -1;
    for (int i = 0; i < n; i++) {
        int count = 0;
        for (int j = 0; j < n; j++) {
            if (strcmp(arr[i], arr[j]) == 0) count++;
        }

        if(count > maxCount) {
            maxCount = count;
            index = i;
        }
    }
    return index;
}

char* convertPort(unsigned short port) {
    static char port_str[10];
    sprintf(port_str,"%d", port);
    return port_str;
}

void* extractAddr(struct sockaddr* sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    } else {
        return &(((struct sockaddr_in6 *) sa)->sin6_addr);
    }
}

int socketConnect(const char* host, const char* port) {
    int status, sock;
    struct addrinfo hints, *servinfo, *p;
    char ipstr[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
        // fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            // perror("client: socket");
            continue;
        }

        if (connect(sock, p->ai_addr, p->ai_addrlen) < 0) {
            // perror("client: connect");
            close(sock);
            continue;
        }

        break;  // connect to the first reachable address we can find
    }

    if (p == NULL) {
        // fprintf(stderr, "client: failed to connect\n");
        return -3;
    }

    inet_ntop(p->ai_family, extractAddr(p->ai_addr), ipstr, sizeof(ipstr));
    // printf("connected to peer node %s (port %s)\n", ipstr, port);

    freeaddrinfo(servinfo);
    return sock;
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
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;  // loop until find an available local address to bind
        }

        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            perror("setsockopt");
            return -4;
        }

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;  // loop until find an available local address to bind
        }
        break;  // bind the first local address we can find
    }

    if (p == NULL) {
        fprintf(stderr, "server: unable to bind an available listener\n");
        return -2;
    }

    freeaddrinfo(servinfo);

    if (listen(listener, backlog) < 0) {
        close(listener);
        return -3;
    }

    return listener;
}

int sendAll(int fd, const char* buf, int* len) {
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

int recvTimeOut(int sd, char* buf, int len, int timeout) {
    struct pollfd pfds;
    pfds.fd = sd;
    pfds.events = POLLIN;

    int n = poll(&pfds, 1, timeout);
    if (n == 0) { return -2; }   // timeout
    if (n == -1) { return -1; }  // error

    return recv(sd, buf, len, 0);  // return # of bytes received, or 0 if connection closed on the other end
}

int readTimeOut(int sd, char* buf, int len, int timeout) {
    struct pollfd pfds;
    pfds.fd = sd;
    pfds.events = POLLIN;

    int n = poll(&pfds, 1, timeout);
    if (n == 0) { return -2; }   // timeout
    if (n == -1) { return -1; }  // error

    return read(sd, buf, len);  // return # of bytes received
}
