/*
** shfd -- Assignment 3 (CS 464/564), Bishop's University
**
** @author:    Wentao Lu (002276355), Yi Ren (002212345)
** @date:      2020/03/09
** @reference: CS 464/564 Course website - https://cs.ubishops.ca/home/cs464
**             Beej's Guide to Network Programming - http://beej.us/guide/bgnet/html/
**             Beej's Guide to Unix IPC - http://beej.us/guide/bgipc/html/single/bgipc.html
**             POSIX thread libraries - http://www.yolinux.com/TUTORIALS/LinuxTutorialPosixThreads.html
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>

#include "../include/utils.h"

#define PORT "9034"
#define BACKLOG 10
#define N_THREADS 256 (< 300)
#define N 1000
#define MEGEXTRA 1000000

pthread_t tid[N_THREADS];

// then update as per Bruda's slide
// create all threads at init, but put them in sleep, wake up only when necessary, how to code?

// if DEBUG, use assert(cond);




struct statistics {
    pthread_mutex_t m;
    pthread_attr_t attr;
    pthread_cond_t cond;
    unsigned short nr;  // number of readers
    unsigned short nw;  // number of writers
    ...
} stats;

int init() {
    // place server in background
    ...

    // redirect I/O file descriptors
    for (int i = getdtablesize() - 1; i >= 0; i--) {
        (void) close(i);
    }
    fd = open("/dev/null", O_RDWR);  // stdin
    (void) dup(fd);  // stdout
    (void) dup(fd);  // stderr

    // detach from tty
    fd = open("/dev/tty", O_RDWR);
    (void) ioctl(fd, TIOCNOTTY, 0);
    (void) close(fd);

    // move server to a safe directory
    (void) chdir("/");

    // set umask
    (void) umask(027);

    // place server into a single process group
    (void) setpgrp(0, getpid());

    // create server's lock file to enforce only 1 copy
    int lf = open("/lf.lock", O_RDWR | O_CREAT, 0640);
    if (lf > 0) {
        perror("server: open lock file");
        exit(1);
    }
    if (flock(lf, LOCK_EX | LOCK_NB)) {
        perror("server: lock file");
        exit(0);
    }

    // save server's process id in lock file
    char pbuf[10];
    (void) sprintf(pbuf, "%6d0", getpid());
    (void) write(lf, pbuf, strlen(pbuf));

    // handle signals
    sa.sa_handler = SIG_IGN;
    // sigemptyset(&sa.sa_mask);
    // sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;  // SA_RESTART restarts the syscall on receiving EINTR
    //
    // registerZombieReaper(&sa, enable);   // enable the reaper before zombies spawn (before any unwaited fork())
    // registerZombieReaper(&sa, disable);  // disable the reaper before any waited fork(), and then restore after wait()
    // volatile sig_atomic_t var;  // can be mutated inside a handler
    // <stdio.h> functions are not async-safe, so cannot be used in handlers
    // void handleSignals(int signal) {
    // 	(void)signal;  // suppress the warning of unused variable
    // 	....           // your own code here must be atomic operations, i.e., not splittable in parts
    // }
    // struct sigaction sa;
    // sigfillset(&sa.sa_mask);  // block all other signals
    // sigemptyset(&sa.sa_mask);  // sa.sa_mask is empty set, no signals will be blocked
    // sa.sa_handler = f;         // register user-defined handler
    // sa.sa_flags = SA_RESTART;  // SA_RESTART restarts the system calls on receiving EINTR
    // if (sigaction(SIGINT, &sa, NULL) == -1) {  // handle SIGINT
    //     perror("sigaction");
    //     exit(1);
    // }
    // if (sigaction(SIGPIPE, &sa, NULL) == -1) {  // handle SIGPIPE
    //     perror("sigaction");
    //     exit(1);
    // }
    // if (sigaction(SIGUSR1, &sa, NULL) == -1) {  // handle SIGUSR1
    //     perror("sigaction");
    //     exit(1);
    // }






    // initialize mutex, condition variable, r/w count
    pthread_mutex_init(&stats.m, NULL);
    pthread_cond_init(&stats.cond, NULL);
    stats.nr = 0;
    stats.nw = 0;

    // initialize thread attribute
    pthread_attr_init(&stats.attr);
    size_t stacksize = sizeof(double) * N * N + MEGEXTRA;
    pthread_attr_setstacksize(&attr, stacksize);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    // round-robin schedule
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 1;
    if (sched_setscheduler(0, SCHED_RR, &param) != 0) {
        perror("sched_setscheduler");
        exit(-1);
    }

    return 0;
}

void reader() {  // MT-safe
    // prepare to read
    pthread_mutex_lock(&stats.m);
    while (stats.nw > 0) {
        pthread_cond_wait(&turn, &stats.m);
    }
    stats.nr++;
    pthread_mutex_unlock(&stats.m);

    // reading...

    // reading finished
    pthread_mutex_lock(&stats.m);
    stats.nr--;
    pthread_cond_broadcast(&turn);
    pthread_mutex_unlock(&stats.m);

    return 0;
}

void writer() {  // MT-safe
    // prepare to write
    pthread_mutex_lock(&stats.m);
    while (stats.nr > 0 || stats.nw > 0) {
        pthread_cond_wait(&turn, &stats.m);
    }
    stats.nw++;
    pthread_mutex_unlock(&stats.m);

    // writing...

    // writing finished
    pthread_mutex_lock(&stats.m);
    stats.nw--;
    pthread_cond_broadcast(&turn);
    pthread_mutex_unlock(&stats.m);

    return 0;
}




void* worker(void* args) {
    char* req = (char*) args;
    ... // tokenize and parse request

    if (strcmp(req[0], "FOPEN") == 0) {
        if (file already opened) {
            pthread_exit((void*)-1);
        }
        open file;
    }

    if (file not opened yet) {
        pthread_exit((void*)-2);
    }

    if (strcmp(req[0], "FSEEK") == 0) {
        ...
    }
    else if (strcmp(req[0], "FREAD") == 0) {
        call read
    }
    else if (strcmp(req[0], "FWRITE") == 0) {
        call write
    }
    else if (strcmp(req[0], "FCLOSE") == 0) {
        ...
    }
    pthread_exit((void*)0);
}

int main(int argc, char* argv[]) {
    struct sigaction sa;
    int rc;  // universal return code for error checking
    void* status;

    if ((rc = init()) != 0) {
        fprintf(stderr, "server: init: failed to initialize server (%d)\n", rc);
        exit(-1);
    }

    int listener, new_fd;
    struct sockaddr_storage cli_addr;
    socklen_t sin_size;
    char buf[256];  // buffer for client data
    char ipstr[INET6_ADDRSTRLEN];

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


    // zombie reaper
    {
        // void enable(int signal) {
        // 	(void)signal;             // suppress the warning of unused variable
        // 	int saved_errno = errno;  // waitpid() might overwrite errno, so we save and restore it
        // 	while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
        // 	errno = saved_errno;
        // }
        //
        // void disable(int signal) {}
        //
        // typedef void (*handler_t)(int);
        //
        // void registerZombieReaper(struct sigaction* sa_ptr, handler_t f) {
        //     sa_ptr->sa_handler = f;  // user-defined handler
        //     if (sigaction(SIGCHLD, sa_ptr, NULL) == -1) {
        //         perror("sigaction");
        //         exit(1);
        //     }
        // }
        //
    }

    char* args[];  // FSEEK identifier offset
    rc = pthread_create(&tid[i], &attr, myfunc, (void*)args);
    if (rc != 0) {
        perror("server: pthread_create");
        exit(-1);
    }

    // for(t=0; t<NUM_THREADS; t++) {
    //    rc = pthread_join(tid[t], &status);
    //    if (rc) {
    //       printf("ERROR; return code from pthread_join() is %d\n", rc);
    //       exit(-1);
    //    }
    //    printf("Main: completed join with thread %ld having a status of %ld\n",t,(long)status);
    // }

    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&stats.m);
    pthread_cond_destroy(&stats.count);
    // pthread_exit(NULL);  // we cannot terminate the main thread for server




    // avoid creating threads before forking
    // child process only has a single thread, which is a clone of the thread that called fork()

    return EXIT_SUCCESS;
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

usleep(10000);  // [0,1000000], MT-Safe, suspends execution of the calling thread for xxx microseconds
