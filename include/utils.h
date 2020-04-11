/*
** utils.h -- some useful wrapper functions
*/

#ifndef _UTILS_H
#define _UTILS_H

#include <stdio.h>
#include <ctype.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <arpa/inet.h>

/*
** read a '\n'-terminated line from a file into buffer, up to a max # of bytes
**
** @param:    a file descriptor, a string buffer, a max size
** @return:   # of bytes read, or -1 on error, or -2 on EOF (no more data)
**            or 0 when an empty line is encountered
*/
int readLine(int file, char* buf, size_t size);

/*
** tokenize a string at blanks and save results in an array
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

size_t str_split(char* str, char** tokens, size_t n);

// check if a string consists of only digits, returns 0(false)/1(true)
int checkDigit(const char* str);

// find the majority index in a char* array, return index or -1 if no majority
int findMajority(char* arr[], int n);

// convert port in decimal number to a string
char* convertPort(unsigned short port);

// extract sin_addr from struct sockaddr, works for both IPv4 and IPv6
void* extractAddr(struct sockaddr* sa);

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
** send string pointed by buf to the file descriptor fd, to a maximum bytes of len
**
** @return:   -1 on failure, 0 on success, and updates the number of bytes actually sent in *len
** @remark:   useful when a large piece of data cannot be completely sent by a single send() call
*/
int sendAll(int fd, const char* buf, int* len);

/*
** a wrapper of recv(sd, buf, len, 0) with a given timeout in milliseconds
**
** @return:   # of bytes received on success, or 0 if connection closed on the other end
**            -1 on error, -2 if timeout has been reached and no data received
*/
int recvTimeOut(int sd, char* buf, int len, int timeout);

// a wrapper of read(sd, buf, len) with a given timeout in milliseconds
int readTimeOut(int sd, char* buf, int len, int timeout);

#endif
