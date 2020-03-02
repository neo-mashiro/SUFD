CS 464/564 Assignment 3
by Wentao Lu, Yi Ren


Content

The content and structure tree of our project is shown below:

.
├── rsshell        // executable in the root directory
├── Makefile       // Makefile in the root directory
├── README.md
├
├── build          // this folder contains all object files
    ├── main.o
    ├── utils.o
├── doc            // this folder contains configuration files and notes
    ├── shconfig
├── include        // this folder contains all header files
    ├── utils.h
├── src            // this folder contains the source code
    ├── main.cpp
    ├── utils.cpp
├── test           // this folder contains test files and output
    ├── test
    ├── testmore


Project description

This program implements a shell which continuously prompts for user input and
then executes the command as requested. The program exits when user types in
`! exit`. Commands can be executed either locally or remotely, in foreground
or in background, depending on the input prefixes. In particular, the local
command `! keepalive` turns on the keepalive mode for remote commands, which
keeps a socket connection until a `! close` command is issued.

When keepalive mode is off, each time a remote command is issued, the program
will establish a new socket connection, interact with the server through this
connection, and then close the connection. If the command is intended to be
run in background, interaction is done in the child process where the socket
connection is copied from the parent, the parent process closes its connection
immediately and gives the prompt back to the user.

When keepalive mode is on, a socket connection is fired up only once for the
1st remote command, and all subsequent remote commands share this same socket
until a local close command is issued. Other than that, nothing is different
from what we mentioned above, except that the parent process of a background
command would continue without closing the connection.


Usage and Demo

make clean && make
./rsshell

! command    // this is a local foreground command
! & command  // this is a local background command
& ! command  // same as above, the order of "&" and "!" doesn't matter

command      // this is a remote foreground command
& command    // this is a remote background command

! keepalive  // turn on keepalive mode
! close      // turn off keepalive mode and disconnect all sockets


Tests

A snippet of our test output can be found at ./test/test

o  If the port number in shconfig is not between 0 and 65535, a "invalid port"
   warning message will be printed to the console, the port number will be set
   to 9001 by default, and a corresponding line will be appended to shconfig.
o  A local command `! cmd` behaves exactly the same as in Assignment 1
o  A local background command behaves exactly the same as in Assignment 1
     ! & cmd
     & ! cmd
o  A remote command such as `hello world` connects to the server, receives
     ACK 1: hello world
     ACK 2: hello world
   then closes the connection and gives prompt back to user
o  A remote background command connects to the server and then immediately
   closes the connection and gives prompt back to user. As soon as the server
   responds, the responses are printed to the console, followed by a message
     & cmd done (0)
   which notifies us that the background command is done with exit code 0
o  When a remote command `quit` is issued, server closes connection on its side
o  When `& quit` is issued, server closes connection in our child process
o  When two remote background commands are issued in rapid succession like
     & a
     & b
   b's response is printed to the console as soon as it's available, even if
   a's response is not yet completely finished, which looks like this:
     ACK 1: a
     ACK 1: b
     ACK 2: a
     ACK 2: b
     & a done (0)
     & b done (0)
   Since the two commands use two different socket connections, this implies
   that the server must have been implemented with some form of concurrency
   that is able to handle multiple clients at the same time. Hence, we got
   the responses back in an interleaved fashion.
o  When keepalive is on, a local command `! cmd` is not affected
o  When keepalive is on, a remote command such as `a` connects to the server,
   prints out the server responses as before, but does not disconnects.
o  Then, a local close command closes the living connection, all subsequent
   remote commands behave just like in the normal mode.
o  When keepalive is on, and two remote background commands are issued in
   rapid succession like
     & a
     & b
   b's response is not printed to the console until a's response is completely
   finished. That is, b's response waits for a's response to finish, they are
   printed to the console in strict sequence.
   This is understandable because now we only have one connection with keepalive
   in effect, the two commands share the same socket to interact with the server,
   one of them must wait for the other, so an interleaved pattern will not be
   observed.

o  Most commands behave alike on the HTTP port 80 of osiris.ubishops.ca
   however, since we don't know how to encode the GET/POST request in C,
   sending the raw request string only receives a "301 redirect" status code,
   and the server side will close the connection each time, so we are not able
   to test keepalive mode on this server.

o  All commands behave in the same manner on SMTP port 25 of linux.ubishops.ca


Reference

o  CS 464/564 Course website - https://cs.ubishops.ca/home/cs464
o  Beej's Guide to Network Programming - http://beej.us/guide/bgnet/html/
o  Beej's Guide to Unix IPC - http://beej.us/guide/bgipc/html/single/bgipc.html


Acknowledgement

o  our code is adapted from solution to Assignment 1 by Dr. Stefan Bruda
   https://cs.ubishops.ca/home/cs464/sshell.tar.gz
o  some changes have been made to accommodate my own modules, and serveral new
   functionalities have been introduced to comply with the requirements of
   Assignment 2
