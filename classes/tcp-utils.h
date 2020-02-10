/*
 * Miscelanious TCP (and general) functions.  Contains functions that
 * connect active sockets, put passive sockets in listening mode, and
 * read from streams.
 *
 * By Stefan Bruda, using the textbook as inspiration.
 */

#ifndef __TCP_UTILS_H
#define __TCP_UTILS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


/*** Error codes: ***/

/* See below for what they mean. */
const int err_host    = -1;
const int err_sock    = -2;
const int err_connect = -3;
const int err_proto   = -4;
const int err_bind    = -5;
const int err_listen  = -6;


/*** Client: ***/

/*
 * Example: connectbyport("linux.ubishops.ca","21");
 *
 * Receives a host name and a port number, the latter as string;
 * attempts then to open a connection to that host on the specified
 * port.  When successful, returns a socket descriptor.  Otherwise
 * returns just as connectbyportint (which see).
 */
int connectbyport(const char*, const char*);

/*
 * Example: connectbyservice("linux.ubishops.ca","ftp");
 *
 * Receives a host name and a service name, and attempts then to open
 * a connection to that host for the specified service.  When
 * successful, returns a socket descriptor.  Otherwise returns just as
 * connectbyportint (which see), plus
 *   err_proto: no port found for the specified service
 */
int connectbyservice(const char*, const char*);

/*
 * Example: connectbyportint("linux.ubishops.ca",21);
 *
 * Receives a host name and a port number, attempts to open a
 * connection to that host on the specified port.  When successful,
 * returns a socket descriptor.  Otherwise returns:
 *   err_host:    error in obtaining host address (h_errno set accordingly)
 *   err_sock:    error in creating socket (errno set accordingly)
 *   err_connect: connection error (errno set accordingly)
 */
int connectbyportint(const char*, const unsigned short);


/*** Server: ***/

/*
 * Example: passivesocketstr("21",10);
 *
 * Receives a port number as a string as well as the maximum length of
 * the queue of pending connections (backlog), and attempts to bind a
 * socket to the given port.  When successful, returns a socket
 * descriptor.  Otherwise returns just as passivesocket (see below).
 */
int passivesocketstr(const char*, const int);

/*
 * Example: passivesocketserv("ftp",10);
 *
 * Receives a service name as well as the maximum length of the queue
 * of pending connections (backlog), and attempts to bind a socket to
 * the port corresponding to the given service.  When successful,
 * returns a socket descriptor.  Otherwise returns just as
 * passivesocket (see below), plus:
 *   err_proto: no port found for the specified service
 */
int passivesocketserv(const char*, const int);

/*
 * Example: passivesocket(21,10);
 *
 * Receives a port number as well as the maximum length of the queue
 * of pending connections, and attempts to bind a socket to the given
 * port.  When successful, returns a socket descriptor.  Otherwise
 * returns:
 *   err_sock:   error in creating socket (errno set accordingly)
 *   err_bind:   bind error (errno set accordingly)
 *   err_listen: error while putting the socket in listening mode
 *               (errno set accordingly)
 */
int passivesocket(const unsigned short, const int);

/*
 * Example: controlsocket(21,10);
 *
 * Behaves just like passivesocket (above), but the resulting socket
 * listent only for local connections (i.e., 127.0.0.1).
 */
int controlsocket(const unsigned short, const int);

/*** Receive stuff: ***/

const int recv_nodata = -2;

/*
 * Behaves just like recv with the flags argument 0, except that it
 * does not block more than the number of miliseconds specified by its
 * last argument.  Returns the number of characters read, or
 * recv_nodata when no data is available, or -1 on error (errno is set
 * accordingly).
 */
int recv_nonblock (const int, char*, const size_t, const int);

/*
 * readline(fd,buf,max) reads a ('\n'-terminated) line from device fd
 * and puts the result in buf.  Does not read more than max
 * bytes. Returns the number of bytes actually read, recv_nodata when
 * no data is available, or -1 on error.  This is a general function
 * but can be of course used with sockets too.
 *
 * Note: the function will return 0 when an empty line is encountered,
 * so a return of 0 is no longer an indication that the end of the
 * file has been reached (check for a recv_nodata return instead).
 */
int readline(const int, char*, const size_t);

#endif /* __TCP_UTILS_H */

