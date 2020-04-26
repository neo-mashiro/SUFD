SUFD: A Simple MultiThread Unix File Daemon in C
================================================

`Repository Statistics API <https://api.github.com/repos/neo-mashiro/SUFD>`_

**Build Status & License & Repo Size & Code Size**

.. image:: https://img.shields.io/travis/neo-mashiro/SUFD/master?label=Master%20Build&style=plastic
   :target: https://travis-ci.com/neo-mashiro/SUFD

.. image:: https://img.shields.io/badge/License-CC0%201.0-blue.svg?style=plastic
   :target: http://creativecommons.org/publicdomain/zero/1.0/

.. image:: https://img.shields.io/github/repo-size/neo-mashiro/SUFD?color=%2300BFFF&label=Repo%20Size&style=plastic
   :target: https://github.com/neo-mashiro/SUFD

.. image:: https://img.shields.io/github/languages/code-size/neo-mashiro/SUFD?color=%20%09%237B68EE&label=Code%20Size&style=plastic
   :target: https://github.com/neo-mashiro/SUFD

-----

**Table of Contents:**

-  `Introduction <#introduction>`__
-  `Features <#features>`__
-  `Installation <#installation>`__
-  `Synopsis <#synopsis>`__
-  `Integration Test <#integration-test>`__
-  `Reference <#reference>`__

-----

Introduction
^^^^^^^^^^^^

SUFD is a simple daemon that simulates a flat-file database that allows multiple clients to connect and access files. It is expected to interact with ``telnet`` or a similar client application. The goal of this project is to practice building a multithread server in a heavy-traffic environment. To do so in a portable manner, this implementation is based on the socket API, Unix IPC and POSIX threads without using any third-party libraries. The daemon binds to one port as a shell server, which accepts shell commands from a local administrator. It also binds to another port as a file server, which serves multiple clients who want to manipulate files.

The shell server is intended for internal use only. It binds to the loopback address with backlog set to 1, so that only 1 local connection can be accepted. Once a command is issued, the output will be stored in a pipe, but won't be sent back until the admin issues a ``cprint``, which prints the output of the last executed shell command. The admin user can disconnect by typing ``quit``, or view the dynamic threads usage information by issuing a ``monitor`` command, this requests the server to continuously send such data per second until the admin hits Enter. If no command has been issued, the session expires after 5 minutes of inactivity.

The file server is able to handle concurrent reads and writes from multiple clients, below is a list of acceptable commands to manipulate files. Note that ``fseek`` is essentially a write request, and ``fclose`` must wait until all readers and writers are done with their work. To eliminate race conditions and ensure data integrity, a simple reader-writer paradigm is implemented with a mutex and a conditional variable so that concurrent reads are allowed while a write request is exclusive. That said, the file access control does not use semaphores to solve the dining philosophers problem, so a writer could possibly starve. To prevent forever idle clients as well as potential deadlocks, a client session quits itself after 1 minute of inactivity.

Upon completion of a shell/file request, the server responses with a line of the form ``status code message``, where ``status`` is either *ok*, *fail* or *err*, indicating if a request has been completed, failed or executed with errors, ``code`` is either 0, a server-side error code or the identifier of a file, and ``message`` is a user-friendly message or the bytes associated with a read/write operation. In particular, if an ``fopen`` request attempts to open a file that has already been opened by clients in other threads, an error response should be expected, whose error code then tells the client which identifier to operate on. To implement this, `open file description locks <https://www.gnu.org/software/libc/manual/html_node/Open-File-Description-Locks.html>`_ have been used to ensure mutual exclusion among distinct client threads.

.. raw:: html

   <div>
       <table style="border:2px solid black;margin-left:auto;margin-right:auto;">
           <tr>
               <th>Command</th>
               <th>Function</th>
           </tr>
           <tr>
               <td>fopen <em>filename</em></td>
               <td>create or open a file (specified by path), return an identifier for future manipulation</td>
           </tr>
           <tr>
               <td>fclose <em>identifier</em></td>
               <td>close the file pointed by <em>identifier</em> so that no further interactions are permitted</td>
           </tr>
           <tr>
               <td>fseek <em>identifier offset</em></td>
               <td>advance the seek pointer by <em>offset</em> bytes from the current position in the file</td>
           </tr>
           <tr>
               <td>fread <em>identifier length</em></td>
               <td>read up to <em>length</em> bytes from the file, return the length and bytes actually read</td>
           </tr>
           <tr>
               <td>fwrite <em>identifier bytes</em></td>
               <td>write up to <em>length</em> bytes to the file, return the length actually wrote and a message</td>
           </tr>
       </table>
   </div>

Features
^^^^^^^^

#. On startup, the server closes all file descriptors, opens or creates a log file *sufd.log* in the root directory, redirects *stdin* 0 to ``/dev/null``, *stdout* 1 and *stderr* 2 to the log file. This file will be locked to enforce only one running copy of the server, for this purpose, a `POSIX record lock <https://gavv.github.io/articles/file-locks/>`_ that ensures mutual exclusion among distinct processes has been used. Then, the server writes its process id to the log file, moves to a safe directory, detaches itself from the *tty*, and puts itself into a single process group. As a result, it will not receive signals from its parent or the init process. In addition, umask will be set up to control the default permission for new files. By default, the server runs in the background to be a daemon in the real sense, unless the debug mode has been activated on the command line.

#. A special monitor thread is being used for concurrency management. Upon startup, it preallocates a batch of ``t_inc`` file threads to handle client requests, which are initially idle and blocked on ``accept()``. Once a file client kicks in, a file thread wakes up to serve the client. The ``accept()`` system call is placed within the critical section to ensure that only 1 thread will wake up at a time. A file thread periodically checks the number of active threads as well as the total number of threads allocated, if there are too many idle threads, it quits itself. While file threads can exit silently in a distributed approach, the monitor thread on the other hand is responsible for overall dynamic threads management. If all preallocated threads are currently active, then another batch of ``t_incr`` threads will be allocated as necessary, as long as the total number of threads does not exceed the limit ``t_max``. Note that any update on the global threads usage data could lead to race conditions. To resolve such conflicts, critical sections have been implemented in all pertinent places.

#. All unwanted signals are explicitly blocked first in the main thread, so that every other thread inherits this signal mask. There's one single thread for handling all signals, it will block on ``sigwait()`` until a signal arrives. Every signal received will be written into the log file, but most of them are just ignored. In particular, the *SIGCHLD* signal is left unhandled since no zombie processes will ever spawn as the server waits for all child processes. However, the following two signals are expressly handled for dynamic reconfiguration.

#. On receiving the *SIGHUP* signal, the server attempts to clean up itself, quit idle threads, shutdown opened file descriptors, free memory and so on. In case some client threads are still active, it waits for them to complete before moving on. After the clean up, the server preallocates a new batch of threads and resumes normal operation.

#. On receiving the *SIGQUIT* signal, the server attempts to clean up itself, quit idle threads, shutdown opened file descriptors, free memory and so on. In case some client threads are still active, it waits for them to complete before moving on. After the clean up, the server terminates gracefully.

Installation
^^^^^^^^^^^^

In a current Linux distribution with a standard C/C++ compiler and a recent version of GNU make.

.. code-block:: shell

    $ mkdir build
    $ make clean && make

Synopsis
^^^^^^^^

Usage: ``./sufd [-t num] [-T num] [-d] [-D] [-v] [-s port] [-f port] -p <host1:port1>..<hostN:portN>``

-d   debug mode, force the daemon to run in foreground and print directly to the console
-D   delay mode, read operations are delayed by 3 seconds and write operations by 6 seconds
-v   verbose mode, a dummy option, not implemented for real
-s   specify the shell port number (9001 by default)
-f   specify the file port number (9002 by default)
-t   specify ``t_inc``, the number of threads to be preallocated (128 by default)
-T   specify ``t_max``, the maximum number of file threads allowed (256 by default)
-p   specify a list of ``host:port`` pairs for the replica servers, not implemented for real

In this application protocol, the ``-p`` option merely serves as a decorator but has no real use, since there are no replica servers. While this program does not account for any synchronization or consistency issues in a distributed context, the other `replica <https://github.com/neo-mashiro/SUFD/tree/replica>`_ branch has a simple solution for peer consensus. In that version, the ``-p`` option is mandatory, so this program is both a server and a client, thus we have more master/slave sockets to handle. In such a setting, any write operation will be passed along to all replica servers (one-phase commit), whoever receives it must synchronize in its local copy, but might suffer from network lags or blocking delay. On the flip side, any read operation will compute the output value based on majority votes, which in some cases may return a *sync fail* response. Anyway, that is just a naive endeavor, so I have included another short report regarding consensus protocols in the *consensus* folder. In a later project using Go, I'll try to implement a distributed key-value store similar to Amazon's Dynamo.

Integration Test
^^^^^^^^^^^^^^^^

First we start the daemon in background, a log file is created. We can play with the file server from multiple terminals using ``telnet``. With the delay mode turned on, it is easy to observe the reader writer synchronization behavior.

.. code-block:: bash

    $ ./sufd -t 4 -T 8 -D
    $ telnet localhost 9002
..

    | Trying 127.0.0.1...
    | Connected to localhost.
    | Escape character is '^]'.
    | Welcome to the database! Please issue your command, or type QUIT to exit.
    | Available commands: FOPEN FSEEK FREAD FWRITE FCLOSE
    | >
    | > fopen test
    | OK 8 file opened successfully
    | > fwrite 8 apple
    | OK 0 data written to the file
    | > fseek 8 -5
    | OK 0 seek pointer is now 0 bytes from the beginning of the file
    | > fread 8 5
    | OK 5 apple
    | > quit
    | Connection closed by foreign host.

Now we connect to the shell server on port 9001, issue a ``monitor`` command to view the threads usage data.

.. code-block:: shell

    $ telnet localhost 9001
..

    | Trying 127.0.0.1...
    | Connected to localhost.
    | Escape character is '^]'.
    | Welcome to the daemon! Please issue your shell command, or type QUIT to exit.
    | You can type MONITOR to view the current threads usage, hit Enter to stop.
    | >
    | > uname -v
    | OK 0 Command execution complete
    | > cprint
    | #86~16.04.1-Ubuntu SMP Mon Jan 20 11:02:50 UTC 2020
    | OK 0 Output printed
    | >
    | > monitor
    | Threads Usage: 1 out of 4 total threads are currently active
    | Threads Usage: 1 out of 4 total threads are currently active
    | ...
    | > quit
    | Connection closed by foreign host.

To test the dynamic threads management, let's simulate some ``telnet`` requests to the file server one at a time per second, put these requests in the background so we don't need to open too many terminals. After 60 seconds, these sessions will automatically expire one by one, so that we don't need to explicitly switch them to the foreground and quit.

.. code-block:: shell

    $ telnet localhost 9002 &
..

    | [25] 28067
    | Trying 127.0.0.1...
    | Connected to localhost.
    | Escape character is '^]'.
    | [25] + 28067 suspended (tty output) telnet localhost 9002
    | ...

.. code-block:: shell

    $ jobs
..

    | [1] suspended (tty output) telnet localhost 9002
    | [2] suspended (tty output) telnet localhost 9002
    | [3] suspended (tty output) telnet localhost 9002
    | ...

.. code-block:: shell

    $ fg
..

    | [1] - 27858 continued telnet localhost 9002
    | Welcome to the database! Please issue your command, or type QUIT to exit.
    | Available commands: FOPEN FSEEK FREAD FWRITE FCLOSE
    | > your session has expired
    | Connection closed by foreign host.
    | ...

As a number of clients have connected to the server, meanwhile we can observe how threads data change over time in the log file. The output is pretty much straightforward: when all the 4 preallocated threads are active, the server allocates another batch of 4 threads. Once the number of threads reaches the limit 8, further connections will be pending in the queue. After 60 seconds, as file clients start to quit and many threads become idle, some exit themselves.

.. code-block:: shell

    $ telnet localhost 9001
..

    | Trying 127.0.0.1...
    | Connected to localhost.
    | Escape character is '^]'.
    | Welcome to the daemon! Please issue your shell command, or type QUIT to exit.
    | You can type MONITOR to view the current threads usage, hit Enter to stop.
    | >
    | > monitor
    | Threads Usage: 0 out of 4 total threads are currently active
    | Threads Usage: 1 out of 4 total threads are currently active
    | Threads Usage: 2 out of 4 total threads are currently active
    | ...
    | Threads Usage: 4 out of 8 total threads are currently active
    | Threads Usage: 5 out of 8 total threads are currently active
    | ...
    | Threads Usage: 8 out of 8 total threads are currently active
    | Threads Usage: 7 out of 8 total threads are currently active
    | ...
    | Threads Usage: 4 out of 8 total threads are currently active
    | Threads Usage: 3 out of 7 total threads are currently active
    | ...
    | Threads Usage: 1 out of 5 total threads are currently active
    | Threads Usage: 0 out of 4 total threads are currently active
    | ...

Now let's send some signals to the server, with the expectation that they will be recorded but ignored.

.. code-block:: shell

    $ kill -SIGINT 27698
    $ kill -SIGPIPE 27698
    $ emacs -nw sufd.log
..

    | ...
    | received signal "Interrupt" (2)
    | received signal "Broken pipe" (13)

When the server receives a *SIGHUP*, it attempts to reload itself, but
will block and wait for busy clients first. This can be seen from the
time difference in the log file as well as by the ``monitor`` command.
After the server completes reloading, it's running like a fresh restart.

.. code-block:: shell

    $ kill -SIGHUP 27698
    $ emacs -nw sufd.log
..

    | ...
    | received signal "Hangup" (1), reloading server...
    | 2020-04-11 10:02:21 (free_server): temporarily closing master socket...
    | 2020-04-11 10:02:23 (free_server): waiting for busy clients...
    | 2020-04-11 10:03:03 closing client connection on socket 7
    | 2020-04-11 10:03:04 closing client connection on socket 10
    | 2020-04-11 10:03:12 (free_server): resetting threads usage...
    | 2020-04-11 10:03:12 (free_server): cleaning up opened files...
    | 2020-04-11 10:03:12 (free_server): freeing allocated thread memory...
    | 2020-04-11 10:03:12 (reset_server): re-establishing master socket connection...
    | 2020-04-11 10:03:12 (reset_server): re-allocating thread pool...
    | 2020-04-11 10:03:12 (reset_server): server reloading complete!

Again let's connect to the file server a few times (in background). This time we send *SIGQUIT* to stop the server. On receiving *SIGQUIT*, the server waits for busy clients and attempts to terminate, see the time difference in the log file.

.. code-block:: shell

    $ kill -SIGQUIT 27698
    $ emacs -nw sufd.log
..

    | ...
    | received signal "Quit" (3), stopping server...
    | 2020-04-11 10:16:25 (free_server): temporarily closing master socket...
    | 2020-04-11 10:16:27 (free_server): waiting for busy clients...
    | 2020-04-11 10:17:14 closing client connection on socket 15
    | 2020-04-11 10:17:15 closing client connection on socket 10
    | 2020-04-11 10:17:16 (free_server): resetting threads usage...
    | 2020-04-11 10:17:16 (free_server): cleaning up opened files...
    | 2020-04-11 10:17:16 (free_server): freeing allocated thread memory...
    | 2020-04-11 10:17:16 (stop_server): destroying locks and mutexes...
    | 2020-04-11 10:17:16 (stop_server): releasing server's lock file...
    | 2020-04-11 10:17:16 (stop_server): server termination complete!

If we need to force stop the server, sending the uncatchable *SIGKILL* will always work.

.. code-block:: shell

    $ kill -9 27698

Reference
^^^^^^^^^

.. [#beej] Beej's Guide to Network Programming - http://beej.us/guide/bgnet/html/
.. [#CSPA] Internetworking with TCP/IP Vol.3: Client-Server Programming and Applications (POSIX Sockets Version)
