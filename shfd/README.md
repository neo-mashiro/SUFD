CS 464/564 Assignment 3
by Wentao Lu, Yi Ren

Project Overview

Our 1200-LOC program implements a multiservice server which has a shell service and
file service. Each client is handled in a single thread to allow for concurrency, the
total number of threads is bounded by 128 using a semaphore(128).

The shell server is implemented using a fork()/execve() paradigm in a thread, where
the child executes the command and writes output to a pipe, the parent reads (block)
output from that pipe and waits for the child to finish. The output will only be
sent to the client when a CPRINT command is issued.

The file server handles file operations. When multiple client threads have race
conditions on the same file, a mutex coupled with a conditional variable is used
to ensure correct synchronization. Read operations are shared, while write operations
are exclusive. However, operations on different files are independent from each other.

In terms of the daemon, it runs in background, closes all file descriptors, redirects
stdin to /dev/null, redirects stdout, stderr to the log file. It will lock the log
file so that there can be only 1 copy of the server running. It also detaches itself
from tty, move itself to a safe directory, sets its own group id and a umask, and
write its process id and other information into the log file. All signals are ignored
so that it will not crash, unless a SIGSTOP or SIGKILL have been raised.

All functionalities are implemented exactly as the handout's requirement, except that
we have added another small feature: when a client has been inactive for 5 minutes,
poll will time out and our server will exit that thread to close the client's session.


Usage

make clean && make
./shfd [-d] [-D] [-s port_number] [-f port_number] [-v]


Passed tests and demos

o  if CPRINT is issued before any shell command has been successfully executed, error
> CPRINT
ERR 5 No command has been issued

o  if CPRINT is issued correctly, last executed shell output will be returned
> date
OK 0 Command executed successfully
> CPRINT
Sat Mar 21 07:18:07 EDT 2020
OK 0 Last executed shell output sent

o  if client just pressed Enter, a new line with prompt will be returned
>

o  if client issued a "quit" command, our server will close that thread
> quit
Connection closed by foreign host.

o  if the shell command is invalid, error
> sadad
ERR 65280 Command executed with errors
> CPRINT
execve: No such file or directory
OK 0 Last executed shell output sent

o  if client is inactive for 5 minutes, server will close its thread
>
> your session has expired
Connection closed by foreign host.

o  if a file command has bad syntax, a hint will be returned
> FREAD 1 2 3 4
FAIL -1 Usage: FREAD identifier length

o  if an undefined file command is issued, error
> FFFFF
FAIL -9 invalid request

o  "FOPEN" opens a file
> FOPEN test
OK 9 file opened successfully

o  if a file has been opened by other clients, "FOPEN" will return the fd
> FOPEN test
ERR 9 file already opened

o  if the filename is not a valid file, "FOPEN" fails
> FOPEN 123456
FAIL 2 cannot open file

o  if the identifier does not refer to a previously opened fd in this session, error
> FSEEK 999 0
ERR 2 invalid identifier, no such file or directory

o  if arguments are not numbers or pointless (e.g. negative length), error
> FSEEK 9 ABCD
FAIL -5 invalid argument(s)
> FREAD 9 -999
FAIL -6 invalid length value

o  "FSEEK" with offset equal to 0 will tell us the current position in the file
> FSEEK 9 0
OK 0 seek pointer is now 0 bytes from the beginning of the file

o  "FSEEK" with offset > 0 will advance the position in the file
> FSEEK 9 20
OK 0 seek pointer is now 20 bytes from the beginning of the file

o  the seek pointer is shared among clients from distinct threads
> FSEEK 9 20
OK 0 seek pointer is now 20 bytes from the beginning of the file

o  "FSEEK" with offset < 0 will move the position backward
> FSEEK 9 -20
OK 0 seek pointer is now 0 bytes from the beginning of the file

o  "FREAD" reads data from the file and advances the seek pointer
> FREAD 9 5
OK 5 11112

o  "FWRITE" writes data to the file and advances the seek pointer
> FWRITE 9 11111
OK 0 wrote 5 bytes data to the file

o  server log shows that multiple threads can read the same file at the same time
client session 1 starts reading test...
client session 2 starts reading test...
client session 1 finished reading test ...
client session 2 finished reading test ...

o  server log shows that a writer thread must wait for all the other readers/writers
client session 2 starts reading test...
client session 2 finished reading test ...
client session 1 starts writing test ...
client session 1 finished writing test ...

o  server log shows that a reader thread must wait for all the other writers
client session 1 starts writing test ...
client session 1 finished writing test ...
client session 2 starts reading test...
client session 2 finished reading test ...

o  server log shows that there can only be at most 1 writer at a time
client session 1 starts writing test ...
client session 1 finished writing test ...
client session 2 starts writing test...
client session 2 finished writing test ...

o  server log shows that operations on different files will not block each other
client session 1 starts writing test ...
client session 2 starts writing test2...
client session 1 finished writing test ...
client session 2 finished writing test2 ...

o  if server is run in background, a log file "shfd.log" will be created/opened and locked:
daemon started, process id is 977

o  we can verify that it is indeed running in background
> ps -ef | grep 977  
neo-mas+   977  1356  0 08:47 ?        00:00:00 ./shfd

o  if we try to run ./shfd twice, an error message will be written into the log file
another copy of the server is already running, exiting pid 1184...

o  even if we send signals to the daemon, it is still running background
> kill -SIGINT 977
> kill -SIGPIPE 977
> ps -ef | grep 977
neo-mas+   977  1356  0 08:47 ?        00:00:00 ./shfd

o  however, a SIGKILL always kills the daemon
> kill -SIGKILL 977
> ps -ef | grep 977

o  finally, this is a sample snippet of our server log "shfd.log"
daemon started, process id is 977
another copy of the server is already running, exiting pid 1184...
new connection from 127.0.0.1 on socket 5
new connection from 127.0.0.1 on socket 8
client session 1 starts reading test...
client session 2 starts reading test...
client session 1 finished reading test ...
client session 2 finished reading test ...
new connection from 127.0.0.1 on socket 10
client session 2 starts reading test...
client session 2 finished reading test ...
client session 3 starts writing test ...
client session 3 finished writing test ...
connection closed by client on socket 10
new connection from 127.0.0.1 on socket 8
closing client connection on socket 5


Reference

o  CS 464/564 Course website - https://cs.ubishops.ca/home/cs464
o  Beej's Guide to Network Programming - http://beej.us/guide/bgnet/html/
o  Beej's Guide to Unix IPC - http://beej.us/guide/bgipc/html/single/bgipc.html
