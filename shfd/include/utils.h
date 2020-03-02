/*
** utils.c -- some useful network wrapper and utility functions
**
** @reference: CS 464/564 by Dr. Stefan Bruda - https://cs.ubishops.ca/home/cs464
**             Beej's Guide to Network Programming - http://beej.us/guide/bgnet/html/
**             Beej's Guide to Unix IPC - http://beej.us/guide/bgipc/html/single/bgipc.html
*/

#ifndef _UTILS_H
#define _UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <arpa/inet.h>

/* error code */
const int err_addr    = -1;
const int err_sock    = -2;
const int err_connect = -3;
const int err_proto   = -4;
const int err_bind    = -5;
const int err_listen  = -6;
const int err_opt     = -7;
const int err_sig     = -8;
const int err_poll    = -9;
const int err_send    = -10;
const int err_recv    = -11;

// convert port in decimal number to a string
char* convertPort(unsigned short port);

// extract sin_addr from struct sockaddr, works for IPv4 and IPv6
void* getInAddr(struct sockaddr* sa);

/*
** open a connection to a host on the specified port
**
** @param:    a host name, a port number
** @return:   a socket descriptor or err_code
** @remark:   the port number can be a string, a service name or a decimal number
** @example:  sock = socketConnect("www.google.com", "80");
**            sock = socketConnect("www.google.com", "http");
**            sock = socketConnect("www.google.com", convertPort(80));
*/
int socketConnect(const char* host, const char* port);

/*
** send a request to server, receive response and print out in real-time
**
** @param:    a socket, a request string, timeout in milliseconds, host name
** @return:   0 on success or errno
*/
int socketTalk(int sockfd, char* req, int timeout, char* host);

/*
** close a socket and reset the file descriptor to -1
**
** @param:    a pointer to a socket file descriptor
** @return:   0 on success or -1 on error
*/
int socketClose(int* sockfd);

/*
** establish a server listener on the specified address and port (or loopback)
**
** @param:    a host name, a port number, a backlog (max # of connections)
** @return:   a listener socket descriptor or err_code
** @remark:   the port number can be a string, a service name or a decimal number
** @example:  listener = setListener(NULL, "9001", 20);             // passive
**            listener = setListener(NULL, "service", 20);          // passive
**            listener = setListener(NULL, convertPort(9001), 20);  // passive
**            listener = setListener("localhost", NULL, 20);        // loopback
*/
int setListener(const char* host, const char* port, int backlog);

/*
** read a '\n'-terminated line from a file into buffer, up to a max # of bytes
** this code is adapted from: https://cs.ubishops.ca/home/cs464/tcp-utils.tar.gz
**
** @param:    a file descriptor, a string buffer, a max size
** @return:   # of bytes read, or -1 on error, or -2 on EOF (no more data)
**            or 0 when an empty line is encountered
*/
int readLine(int file, char* buf, size_t size);

/*
** tokenize a string at blanks and save results in an array
** this code is adapted from: https://cs.ubishops.ca/home/cs464/tokenize.tar.gz
**
** @param:    a string str of length n, an array tokens
** @return:   # of tokens tokenized
** @remark:   continuous sequences of blanks are treated as one blank
**            this function mutates the first argument str
**            --------------------------------------------------
** @example:  char str[100] = "sick and   tired of tokenizers.";
**            char* tokens[strlen(str)];
**            int n = tokenize(str, tokens, strlen(str));
**            for (int i = 0; i < n; i++) {
**                cout << i << ": " << tokens[i] << "\n";
**            }
** @produce:  0: sick
**            1: and
**            2: tired
**            3: of
**            4: tokenizers.
*/
size_t tokenize(char* str, char** tokens, size_t n);

#endif /* _UTILS_H */
