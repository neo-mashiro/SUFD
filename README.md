CS 464/564 Assignment 4 by
  - Wentao Lu (002276355) WLU19@UBishops.ca
  - Yi Ren (002269013) YREN19@UBishops.ca


# PROJECT OVERVIEW

  Our implementation has several threads.

  First, we have a shell thread intended for internal use only. This thread is bound  
  to the loopback address with backlog set to 1, so that only 1 local connection
  can be accepted. In order to test threads data with ease, this time we have added
  a new command "monitor". When "monitor" is issued, the server will send threads
  usage information every second until the admin user hits Enter.

  Then, we have a monitor thread which is used to preallocate a number of threads
  and dynamically allocate more threads as necessary. The preallocated threads are
  idle at first and will block on accept() until a file client kicks in, the accept
  call is within the critical region to ensure that only 1 thread will wake up at
  a time. When a preallocated file thread is done serving the client, it checks
  the threads usage data, if there are too many idle threads, it will quit itself.

  In the main thread, all unwanted signals are explicitly blocked. Therefore, every
  thread inherits this sigmask. To handle signals, we create one single thread for
  handling all signals. This thread will block on sigwait() until a signal arrives,
  every signal received is written into the log file. When SIGHUP/SIGQUIT is received,
  the server will change the file master socket to -1 so that no more clients will
  be accepted. While some clients are still busy, they will eventually finish their
  work and go back to the accept() call, which then returns -1 and set the errno to
  EBADF. In this case, we let the client thread to exit normally. Finally when there
  are no more active client threads, the server cleans up itself, shutdown opened
  file descriptors, free memory, destroy mutexes and so on.

  To handle replica servers, we created another 2 threads. One of them listens for
  peers to connect, and then create a sync thread for each peer to receive sync
  requests from that peer. The other one is responsible for connecting to other
  peers from a client perspective, once connected, the slave sockets will be used
  to send sync requests to the peers.


# TEST SCENARIO 1

Step 1: first we test the command line switches. If the option "-p" or its arguments
        are missing, an error message will be printed to the console. However, if
        the option "-t" and "-T" are not given, our program will set t_incr to 8
        and t_max to 24 by default.

        Usage: ./shfd -p <host1:port1>..<hostN:portN> [-t] [-T] [-d] [-D] [-v] [-s port] [-f port]

Step 2: run the server on a single node. (If the array of parameters <host:port>
        are not given on the command line, our program runs as a single server.)

        > make clean && make
        > ./shfd -s 8000 -f 9000 -p 10000 -t 8 -T 24 -D

Step 3: now the daemon has started in the background, a log file is created. To see
        the updated log file, we use the following command. For convenience, the
        process id of the daemon is written to the first line of the log file.

        > cat shfd.log  

Step 4: now we play with the file server from multiple terminals using "telnet",
        we can easily observe the same reader/writer synchronization behaviour as in
        the previous assignment by looking at the log file.

        > telnet localhost 9000

Step 5: now we connect to the shell service on port 8000. Our program binds the port
        to the loopback address, so only local connections can be accepted, it's
        for internal admin use only. Besides, we set the backlog to 1, so at most 1
        connection will be accepted, another connection will block.

        > telnet localhost 8000

Step 6: In addition to the normal shell commands, this time we have added a new
        shell command "monitor" to facilitate our test. When "monitor" is issued in
        the shell service, the client can see the current threads usage data, such
        data will be updated and printed to the console every second, until the user
        hits Enter.

        > telnet localhost 8000
        > monitor
        Threads Usage: 0 out of 8 total threads are currently active
        Threads Usage: 0 out of 8 total threads are currently active
        ...

Step 7: now we have preallocated 8 threads that are blocked on accept(), while the
        max number of threads is t_max = 24. To test the dynamic threads management
        functionality, we run the shell script "test.sh" in this folder. This shell
        script simulates 32 "telnet" requests to the file service one at a time per
        second, these requests are put in the background so we don't need to open
        too many terminals.

        > cat test.sh
        > ./test.sh

        Alternatively, we can place telnet in background many times to simulate clients.

        > telnet localhost 9000 &
        > telnet localhost 9000 &
        ...

Step 8: now that a lot of clients have connected to the server, we can use the
        "monitor" command in the shell service to view the dynamic threads usage.
        We also added another feature that a file client will automatically quit
        after 1 minute of inactivity (his session has expired), so that we don't
        need to explicitly switch them to the foreground and quit.
        The output below is straightforward: when all the 8 preallocated threads
        are active, the server allocates another batch of 8 threads. When the number
        of threads reaches 24, further connections will be pending in the queue.
        After 1 minute, as file clients start to quit one by one, the number of
        active threads decreases, and some threads will exit.

        > telnet localhost 8000
        > monitor
        Threads Usage: 0 out of 8 total threads are currently active
        Threads Usage: 1 out of 8 total threads are currently active
        Threads Usage: 2 out of 8 total threads are currently active
        Threads Usage: 3 out of 8 total threads are currently active
        Threads Usage: 4 out of 8 total threads are currently active
        Threads Usage: 5 out of 8 total threads are currently active
        Threads Usage: 6 out of 8 total threads are currently active
        Threads Usage: 7 out of 8 total threads are currently active
        Threads Usage: 8 out of 16 total threads are currently active
        Threads Usage: 9 out of 16 total threads are currently active
        Threads Usage: 10 out of 16 total threads are currently active
        Threads Usage: 11 out of 16 total threads are currently active
        Threads Usage: 12 out of 16 total threads are currently active
        Threads Usage: 13 out of 16 total threads are currently active
        Threads Usage: 14 out of 16 total threads are currently active
        Threads Usage: 15 out of 16 total threads are currently active
        Threads Usage: 16 out of 24 total threads are currently active
        Threads Usage: 17 out of 24 total threads are currently active
        Threads Usage: 18 out of 24 total threads are currently active
        Threads Usage: 19 out of 24 total threads are currently active
        Threads Usage: 20 out of 24 total threads are currently active
        Threads Usage: 21 out of 24 total threads are currently active
        Threads Usage: 22 out of 24 total threads are currently active
        Threads Usage: 23 out of 24 total threads are currently active
        Threads Usage: 24 out of 24 total threads are currently active
        Threads Usage: 24 out of 24 total threads are currently active
        ...
        Threads Usage: 23 out of 24 total threads are currently active
        Threads Usage: 22 out of 24 total threads are currently active
        Threads Usage: 21 out of 24 total threads are currently active
        Threads Usage: 20 out of 24 total threads are currently active
        Threads Usage: 19 out of 24 total threads are currently active
        Threads Usage: 18 out of 24 total threads are currently active
        Threads Usage: 17 out of 24 total threads are currently active
        Threads Usage: 16 out of 24 total threads are currently active
        Threads Usage: 15 out of 23 total threads are currently active
        Threads Usage: 14 out of 22 total threads are currently active
        Threads Usage: 13 out of 21 total threads are currently active
        Threads Usage: 12 out of 20 total threads are currently active
        Threads Usage: 11 out of 19 total threads are currently active
        Threads Usage: 10 out of 18 total threads are currently active
        Threads Usage: 9 out of 17 total threads are currently active
        Threads Usage: 8 out of 16 total threads are currently active
        Threads Usage: 7 out of 15 total threads are currently active
        Threads Usage: 6 out of 14 total threads are currently active
        Threads Usage: 5 out of 13 total threads are currently active
        Threads Usage: 4 out of 12 total threads are currently active
        Threads Usage: 3 out of 11 total threads are currently active
        Threads Usage: 2 out of 10 total threads are currently active
        Threads Usage: 1 out of 9 total threads are currently active
        Threads Usage: 0 out of 8 total threads are currently active

Step 9: now we try to send some signals to the server, but the server just ignore
        them. The log file will record the signals they received.

        > kill -SIGINT 27795
        > kill -SIGPIPE 27795
        > cat shfd.log
        ...
        received signal "Interrupt" (2)
        received signal "Broken pipe" (13)

Step 10: exit the server

         > kill -9 27795


# TEST SCENARIO 2

Step 1: first we start the server in background
        > ./shfd -s 8000 -f 9000 -p 10000 -t 8 -T 24 -D

        then we connect to the file server a few times (in background).

        > telnet localhost 9000 &
        > telnet localhost 9000 &
        ...

        then monitor thread usage in the shell window.

        > telnet localhost 8000
        > monitor

        then we send SIGHUP to the server.

        > kill -SIGHUP 30200

        on receiving SIGHUP, the server attempts to reload itself, but it will
        block and wait for busy clients since we still have some active file clients.
        This can be seen from the time difference in the log file as well as by
        the monitor command.

        > cat shfd.log
        received signal "Hangup" (1), reloading server...
        2020-04-11 10:02:21  (free_server): temporarily closing master socket...
        2020-04-11 10:02:23  (free_server): waiting for busy clients...
        2020-04-11 10:03:03  closing client connection on socket 7
        2020-04-11 10:03:04  closing client connection on socket 10
        2020-04-11 10:03:04  closing client connection on socket 11
        2020-04-11 10:03:05  closing client connection on socket 12
        2020-04-11 10:03:05  closing client connection on socket 13
        2020-04-11 10:03:11  closing client connection on socket 14
        2020-04-11 10:03:12  (free_server): resetting threads usage...
        2020-04-11 10:03:12  (free_server): cleaning up opened files...
        2020-04-11 10:03:12  (free_server): freeing allocated thread memory...
        2020-04-11 10:03:12  (reset_server): re-establishing master socket connection...
        2020-04-11 10:03:12  (reset_server): re-allocating thread pool...
        2020-04-11 10:03:12  (reset_server): server reloading complete!

        after the server completes reloading, it's like a fresh restart, no matter
        how many threads used to be working, now we have a new batch of 8 threads,
        and the server is still active, as we can tell from the shell service.

Step 2: again we try to connect to the file server a few times (in background).

        > telnet localhost 9000 &
        > telnet localhost 9000 &
        ...

        then we send SIGQUIT to the server.

        > kill -SIGQUIT 30200

        on receiving SIGQUIT, the server attempts to terminate, but it will wait
        for busy clients. This can be seen from the time difference in the log
        file as well as by the monitor command.

        received signal "Quit" (3), stopping server...
        2020-04-11 10:16:25  (free_server): temporarily closing master socket...
        2020-04-11 10:16:27  (free_server): waiting for busy clients...
        2020-04-11 10:17:14  closing client connection on socket 15
        2020-04-11 10:17:15  closing client connection on socket 10
        2020-04-11 10:17:15  closing client connection on socket 9
        2020-04-11 10:17:16  (free_server): resetting threads usage...
        2020-04-11 10:17:16  (free_server): cleaning up opened files...
        2020-04-11 10:17:16  (free_server): freeing allocated thread memory...
        2020-04-11 10:17:16  (stop_server): destroying locks and mutexes...
        2020-04-11 10:17:16  (stop_server): releasing server's lock file...
        2020-04-11 10:17:16  (stop_server): server termination complete!

        after the server completes terminations, we cannot connect to it anymore,
        and in the shell window we can see a message:
        "Connection closed by foreign host".


# TEST SCENARIO 3

Now we test our program on multiple servers, assume we have 3 replica nodes:
node 0 (the current node), node 1 (peer 1), node 2 (peer 2)

For simplicity, assume that each node has the following port numbers:

            shell port     file port     peer port
node 0:        8000           9000         10000
node 1:        8001           9001         10001
node 2:        8002           9002         10002

Step 1: build and run shfd in the current folder, specify peers on the command line

        > make clean && make
        > ./shfd -s 8000 -f 9000 -p 10000 localhost:10001 localhost:10002

Step 2: copy "shfd" to subfolders "node1" and "node2"

        > cp shfd node1
        > cp shfd node2

Step 3: run peer node 1 in subfolder "node1"

        > cd node1
        > ./shfd -s 8001 -f 9001 -p 10001 localhost:10000 localhost:10002

Step 4: run peer node 2 in subfolder "node2"

        > cd ../node2
        > ./shfd -s 8002 -f 9002 -p 10002 localhost:10000 localhost:10001

Step 5: now we have all 3 nodes running in background, the log file of each node
        indicates that all peers are now connected.

        > cat shfd.log
        daemon started, process id is 32203
        2020-04-11 11:07:51  connected to peer node localhost (port 10000)
        2020-04-11 11:07:51  connected to peer node localhost (port 10001)

Step 6: we pick an arbitrary node, connect to its file service, open a file called
        "test", then we issue some "fwrite" and "fseek" commands.

        > telnet localhost 9002
        > fopen test
        > fseek 13 0
        > fwrite 13 111112222233333
        ...

        after each command, the 3 nodes are synchronized. When we open a file "test"
        on node 2 in the subfolder "node2", another 2 files also called "test" are
        created in the parent folder and the subfolder "node1". When we seek or
        write some bytes of data to "test" on one node, the "test" files in the
        other 2 folders are synchronized as well. The only thing to notice is that
        the identifier of the same file could vary on different nodes.

Step 7: now we try to issue many concurrent commands on different nodes, then check
        the "test" file in each folder. They are still synchronized because each
        command happens almost instantly. However, in a real production environment
        where the number of clients is huge on each node, or perhaps the nodes are
        far away from each other so that network has high latency, the "test" data
        could be inconsistent on different nodes.

        Now the "test" file has data "apple", to simulate such data inconsistency,
        we manually change the data to "lemon" on node 1.

        > cd node1
        > emacs test  # manually change apple to lemon

        then if we issue a "fread" command on whichever node, the result will still
        be "apple" since it's the majority.

        > fopen test
        > fseek 13 -5  # move the seek pointer back to the beginning of file
        > fread 13 5  # apple
        > fseek 13 -5  # move the seek pointer back to the beginning of file

        now we also manually change the data to "lemon" on node 2, then a "fread"
        command on whichever node will return "lemon" since now "lemon" is the
        majority.

        To read the correct value, we must use "fseek" to move the seek pointer
        as appropriate. If a "fread" command is issued when the seek pointer is
        at the end of file, "FAIL 99 SYNC FAIL" will be returned.


# Reference

o  CS 464/564 Course website - https://cs.ubishops.ca/home/cs464
o  Beej's Guide to Network Programming - http://beej.us/guide/bgnet/html/
o  Beej's Guide to Unix IPC - http://beej.us/guide/bgipc/html/single/bgipc.html
