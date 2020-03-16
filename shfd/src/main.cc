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

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
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
#include <time.h>
#include <signal.h>
#include <ctype.h>

#include "../include/utils.h"

#define N_THREADS 128
#define N 1000
#define MEGEXTRA 1000000

const char* path[] = {"/bin", "/usr/bin", 0};


// then update as per Bruda's slide

// if DEBUG, use assert(cond); and     sched_yield();     sleep(3); suspends execution


struct monitor_t {          // struct for monitor data
    pthread_mutex_t m_mtx;  // monitor mutex
    int num_conn;           // number of active connections
    int max_conn;           // highest historical number of connections
    int tot_conn;           // total number of connections served
    int tot_time;           // total processing time
} monitor;

struct lock_t {               // struct for file access control
    char* f_name;             // file name (path)
    int fd;                   // file descriptor (identifier)
    pthread_mutex_t f_mtx;    // file access mutex
    pthread_cond_t f_cond;    // file access condition variable
    unsigned short n_reader;  // number of readers
    unsigned short n_writer;  // number of writers, 0 or 1, at most 1
};

struct echo_t {     // struct for server response
    pthread_t tid;  // unique thread id
    char* status;   // OK / FAIL / ERR
    int code;       // server side error code
    char* message;  // client-friendly message
};

struct echo_t echos[1024];  // every thread (client) receives a server response
struct lock_t locks[1024];  // every filename is associated with a unique lock
int n_echo = 0;  // number of echos used
int n_lock = 0;  // number of locks used

sem_t s_sem;  // semaphore for limiting the number of shell threads
sem_t f_sem;  // semaphore for limiting the number of file threads
pthread_attr_t attr;  // universal thread attribute
pthread_mutex_t mutex;  // mutex for safely updating global counts (n_echo and n_lock)

int init_server() {
    // // redirect I/O file descriptors
    // for (int i = getdtablesize() - 1; i >= 0; i--) {
    //     (void) close(i);
    // }
    // fd = open("/dev/null", O_RDWR);  // stdin
    // (void) dup(fd);  // stdout
    // (void) dup(fd);  // stderr
    //
    // // detach from tty
    // fd = open("/dev/tty", O_RDWR);
    // (void) ioctl(fd, TIOCNOTTY, 0);
    // (void) close(fd);
    //
    // // move server to a safe directory
    // (void) chdir("/");
    //
    // // set umask
    // (void) umask(027);
    //
    // // place server into a single process group
    // (void) setpgrp(0, getpid());
    //
    // // create server's lock file to enforce only 1 copy
    // int lf = open("/lf.lock", O_RDWR | O_CREAT, 0640);
    // if (lf > 0) {
    //     perror("server: open lock file");
    //     exit(1);
    // }
    // if (flock(lf, LOCK_EX | LOCK_NB)) {
    //     perror("server: lock file");
    //     exit(0);
    // }
    //
    // // save server's process id in lock file
    // char pbuf[10];
    // (void) sprintf(pbuf, "%6d0", getpid());
    // (void) write(lf, pbuf, strlen(pbuf));

    // handle signals
    struct sigaction sa;
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


    // initialize semaphore, mutex, condition variable, thread attribute
    sem_init(&s_sem, 0, N_THREADS);
    sem_init(&f_sem, 0, N_THREADS);
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&monitor.m_mtx, NULL);
    pthread_attr_init(&attr);
    size_t stacksize = sizeof(double) * N * N + MEGEXTRA;
    pthread_attr_setstacksize(&attr, stacksize);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);  // every client is independent

    // set round-robin schedule
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 1;
    if (sched_setscheduler(0, SCHED_RR, &param) != 0) {
        perror("sched_setscheduler");
        exit(1);
    }

    return 0;
}

int exit_server() {
    // release resources
    sem_destroy(&s_sem);
    sem_destroy(&f_sem);
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&monitor.m_mtx);

    for (int i = 0; i < n_lock; i++) {
        unlink(locks[i].f_name);
        close(locks[i].fd);
        pthread_mutex_destroy(&locks[i].f_mtx);
        pthread_cond_destroy(&locks[i].f_cond);
    }
    pthread_exit(NULL);  // exit the main thread
}

/*
void* monitor(void*) {
    while (1) {
        sleep(1200);  // report server statistics every 2 minutes
        pthread_mutex_lock(&stats.mutex);
        time_t now = time(0);  // create a timer pointer
        printf("Statraks: %s\n", ctime(&now));  // local calendar time
        printf("Statraks: total number of connections served: %d\n", stats.tot_conn);
        printf("Statraks: number of currently active connections: %d\n", stats.num_conn);
        printf("Statraks: highest historical number of connections: %d\n", stats.max_conn);
        printf("Statraks: average connection time: %d seconds\n", (int)((float)stats.tot_time/(float)max(stats.tot_conn, 1)));
        pthread_mutex_unlock(&stats.mutex);
    }
    pthread_exit(NULL);
}
*/
int opener(int argc, const char** argv, int echo_id) {
    // validate request format
    if (argc != 2) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -1;
        echos[echo_id].message = "Usage: FOPEN filename";
        return 0;
    }
    char* filename = argv[1];

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;  // lock to EOF

    int fd = open(filename, O_RDWR);  // if file already opened by another thread, we get a new fd for the same file
    if (fd < 0) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = errno;
        echos[echo_id].message = "cannot open file";
        return 0;
    }

    if (fcntl(fd, F_SETLK, &fl) == -1) {  // file descriptors of the same file in the same process share the lock
        close(fd);  // when file already opened, we just close this new fd and use the previously opened fd
        echos[echo_id].status = "ERR";
        echos[echo_id].message = "file already opened";
        // find identifier of the already opened file
        for (int i = 0; i < n_lock; i++) {
            if (strcmp(locks[i].f_name, filename) == 0) {
                echos[echo_id].code = locks[i].fd;  // identifier
                return i;  // lock_id
            }
        }
    }

    // if we reach this, file is opened for the 1st time
    // obtain an entry in locks[] specified by lock_id, and increment n_lock, MT-safe
    pthread_mutex_lock(mutex);
    int lock_id = n_lock;
    n_lock++;
    pthread_mutex_unlock(mutex);

    // update shared struct lock_t, prepare file for future manipulation
    locks[lock_id].f_name = filename;
    locks[lock_id].fd = fd;
    locks[lock_id].n_reader = 0;
    locks[lock_id].n_writer = 0;
    pthread_mutex_init(&locks[lock_id].f_mtx, NULL);
    pthread_cond_init(&locks[lock_id].f_cond, NULL);

    // success response
    echos[echo_id].status = "OK";
    echos[echo_id].code = fd;
    echos[echo_id].message = "file opened successfully";

    return lock_id;
}

int seeker(int argc, const char** argv, int echo_id, int lock_id) {
    // validate request format
    if (argc != 3) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -1;
        echos[echo_id].message = "Usage: FSEEK identifier offset";
        return 0;
    }
    if (checkDigit(argv[1]) == 0 || checkDigit(argv[2]) == 0) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -5;
        echos[echo_id].message = "invalid argument(s)";
        return 0;
    }
    if (argv[2] < 0) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -6;
        echos[echo_id].message = "invalid offset value";
        return 0;
    }

    int identifier = argv[1];
    int offset = argv[2];
    struct lock_t lock = locks[lock_id];

    if (identifier != lock.fd || lock.fd <= 0) {
        echos[echo_id].status = "ERR";
        echos[echo_id].code = ENOENT;
        echos[echo_id].message = "invalid identifier, no such file or directory";
        return 0;
    }

    // waiting for resources
    pthread_mutex_lock(&lock.f_mtx);
    while (lock.n_writer > 0) {
        pthread_cond_wait(&lock.f_cond, &lock.f_mtx);
    }
    lock.n_reader++;
    pthread_mutex_unlock(&lock.f_mtx);

    // seeking... essentially, seeking is just the same as reading
    // each distinct opened fd of this file has an independent seek pointer
    if (lseek(identifier, offset, SEEK_CUR) == -1) {  // lseek() is atomic just like read() and write()
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = errno;
        echos[echo_id].message = "system call lseek() returns -1";
        return 0;
    }

    // seeking finished
    pthread_mutex_lock(&lock.f_mtx);
    lock.n_reader--;
    pthread_cond_broadcast(&lock.f_cond);
    pthread_mutex_unlock(&lock.f_mtx);

    // success response
    echos[echo_id].status = "OK";
    echos[echo_id].code = 0;
    sprintf(echos[echo_id].message, "seek pointer advanced by %d bytes", offset);

    return 0;
}

int reader(int argc, const char** argv, int echo_id, int lock_id) {
    // validate request format
    if (argc != 3) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -1;
        echos[echo_id].message = "Usage: FREAD identifier length";
        return 0;
    }
    if (checkDigit(argv[1]) == 0 || checkDigit(argv[2]) == 0) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -5;
        echos[echo_id].message = "invalid argument(s)";
        return 0;
    }
    if (argv[2] < 0) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -6;
        echos[echo_id].message = "invalid length value";
        return 0;
    }

    int identifier = argv[1];
    int len = argv[2];
    struct lock_t lock = locks[lock_id];

    if (identifier != lock.fd || lock.fd <= 0) {
        echos[echo_id].status = "ERR";
        echos[echo_id].code = ENOENT;
        echos[echo_id].message = "invalid identifier, no such file or directory";
        return 0;
    }

    // waiting for resources
    pthread_mutex_lock(&lock.f_mtx);
    while (lock.n_writer > 0) {
        pthread_cond_wait(&lock.f_cond, &lock.f_mtx);
    }
    lock.n_reader++;
    pthread_mutex_unlock(&lock.f_mtx);

    // reading...
    char buf[4096];
    int n = read(lock.fd, buf, len);
    if (n == -1) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = errno;
        echos[echo_id].message = "system call read() returns -1";
        return 0;
    }
    if (buf[strlen(buf) - 1] == '\n') {
        buf[strlen(buf) - 1] = '\0';
    }

    // reading finished
    pthread_mutex_lock(&lock.f_mtx);
    lock.n_reader--;
    pthread_cond_broadcast(&lock.f_cond);
    pthread_mutex_unlock(&lock.f_mtx);

    // success response
    echos[echo_id].status = "OK";
    echos[echo_id].code = n;
    echos[echo_id].message = buf;

    return 0;
}

int writer(int argc, const char** argv, int echo_id, int lock_id) {
    // validate request format
    if (argc != 3) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -1;
        echos[echo_id].message = "Usage: FWRITE identifier bytes";
        return 0;
    }
    if (checkDigit(argv[1]) == 0) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -5;
        echos[echo_id].message = "invalid argument(s)";
        return 0;
    }

    int identifier = argv[1];
    char* buf = argv[2];

    struct lock_t lock = locks[lock_id];

    if (identifier != lock.fd || lock.fd <= 0) {
        echos[echo_id].status = "ERR";
        echos[echo_id].code = ENOENT;
        echos[echo_id].message = "invalid identifier, no such file or directory";
        return 0;
    }

    // waiting for resources
    pthread_mutex_lock(&lock.f_mtx);
    while (lock.n_reader > 0 || lock.n_writer > 0) {
        pthread_cond_wait(&lock.f_cond, &lock.f_mtx);
    }
    lock.n_writer++;
    pthread_mutex_unlock(&lock.f_mtx);

    // writing...
    int len = strlen(buf);
    int total = 0;   // bytes sent
    int left = len;  // bytes left
    int n;
    while (total < left) {
        n = write(fd, buf + total, left);  // update seek
        if (n == -1) {
            echos[echo_id].status = "FAIL";
            echos[echo_id].code = errno;
            echos[echo_id].message = "system call write() returns -1";
            return 0;
        }
        total += n;
        left -= n;
    }

    // writing finished
    pthread_mutex_lock(&lock.f_mtx);
    lock.n_writer--;
    pthread_cond_broadcast(&lock.f_cond);
    pthread_mutex_unlock(&lock.f_mtx);

    // success response
    echos[echo_id].status = "OK";
    echos[echo_id].code = 0;
    sprintf(echos[echo_id].message, "wrote %d bytes data to the file", total);

    return 0;
}

int closer(int argc, const char** argv, int echo_id, int lock_id) {
    // validate request format
    if (argc != 2) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -1;
        echos[echo_id].message = "Usage: FCLOSE identifier";
        return 0;
    }
    if (checkDigit(argv[1]) == 0) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -5;
        echos[echo_id].message = "invalid argument(s)";
        return 0;
    }

    int identifier = argv[1];
    struct lock_t lock = locks[lock_id];

    if (identifier != lock.fd || lock.fd <= 0) {
        echos[echo_id].status = "ERR";
        echos[echo_id].code = ENOENT;
        echos[echo_id].message = "invalid identifier, no such file or directory";
        return 0;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(identifier, F_SETLK, &fl) == -1) {  // unlock file
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = errno;
        echos[echo_id].message = "cannot close (unlock) file";
        return 0;
    }

    // when we close this fd, all the locks on this file in the same process are released
    // even if the locks were made using other file descriptors that remain open
    if (close(identifier) < 0) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = errno;
        echos[echo_id].message = "cannot close file";
        return 0;
    }

    // success response
    echos[echo_id].status = "OK";
    echos[echo_id].code = 0;
    echos[echo_id].message = "file closed";

    // upon success of a close() command, we should also safely decrement n_lock
    // and remove the corresponding entry from locks[]. Unfortunately, it's hard
    // to do so in C with merely a static-allocated C-array given the complexity
    // of a multithreading sychronization context. It would be much nicer if dynamic
    // associated data structures such as C++ std::unordered_map is used instead
    // but again atomicity and MT-safe features must be guaranteed.
    // In this implementation, an entry once polluted will not be reusable later
    // on. Hence, the array locks[1024] will eventually become exhausted with its
    // index stepping out of bound, which leads to a SIGSEGV signal and takes down
    // the server. Having said that though, 1024 is a reasonable limit that works
    // fine with a medium amount of clients.
    return 0;
}

void strike(int sockfd, sem_t* sem_mutex) {
    // close socket, release semaphore and exit thread
    shutdown(sockfd, SHUT_WR);  // civilized server shutdown first before close
    close(sockfd);
    sem_post(sem_mutex);
    pthread_exit((void*)-1);  // exit status will not be received by pthread_join() though...
}

void* s_worker(void* csock) {
    // wait semaphore
    sem_wait(&s_sem);

    // obtain an entry in echos[] specified by echo_id, and increment n_echos, MT-safe
    pthread_mutex_lock(mutex);
    int echo_id = n_echo;
    n_echo++;
    pthread_mutex_unlock(mutex);
    echos[echo_id].tid = pthread_self();

    // add client socket to poll()
    int sock = *((int*)csock);
    struct pollfd cfds[1];
    cfds[0].fd = sock;
    cfds[0].events = POLLIN;
    int n_res;

    int executed = 0;  // check if a shell command has been issued

    // repeatedly receive a request from client and handle it
    while (1) {
        send("%s", "> ");  // change send
        if ((n_res = poll(cfds, 1, 60000)) != 0) {  // time out after 1 minute of inactivity
            if (n_res < 0) {
                perror("poll");
                strike(sock, &s_sem);  // worker go on strike
            }

            char req[256];
            memset(req, 0, sizeof(req));
            int n_bytes;  // number of bytes received

            // receive a client request
            if (cfds[0].revents & POLLIN) {
                n_bytes = recv(sock, req, sizeof(req) - 1, 0);
                if (n_bytes == 0) {
                    printf("connection closed by client on socket %d\n", sock);
                    strike(sock, &s_sem);
                }
                else if (n_bytes < 0) {
                    perror("recv");
                    strike(sock, &s_sem);
                }
            }

            // replace the newline
            if (strlen(req) > 0 && req[strlen(req) - 1] == '\n') {
                req[strlen(req) - 1] = '\0';
            }
            if (VERBOSE_MODE) {
                printf("received client request: %s\n", req);
            }

            // if client just pressed Enter('\n'), start over
            if (strlen(req) == 0) {
                continue;
            }

            // execute command from client
            // // create a pipe
            // int pipefd[2];
            // if (pipe(pipefd) == -1) {
            //     perror("pipe");
            //     exit(1);
            // }
            //
            // int status = 0;
            // pid_t child = fork();
            //
            // if (child == -1) {
            //     perror("fork");
            //     exit(1);
            // }
            // // fork() and exec() the shell command in child process
            // else if (child == 0) {  // child
            //     while ((dup2(pipefd[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}  // redirect child's STDOUT to pipefd[1]
            //     while ((dup2(pipefd[1], STDERR_FILENO) == -1) && (errno == EINTR)) {}  // redirect child's STDERR to pipefd[1]
            //     close(pipefd[0]);
            //     close(pipefd[1]);
            //     execve(command, argv, envp);  // attempt to execute with no path prefix ...
            //     for (size_t i = 0; path[i] != 0; i++) {  // then try with path prefixed
            //         char* cp = new char[strlen(path[i]) + strlen(command) + 2];
            //         sprintf(cp, "%s/%s", path[i], command);
            //         execve(cp, argv, envp);
            //         delete[] cp;
            //     }
            //     // if execve() failed and errno set
            //     char* message = new char[strlen(command) + 10];
            //     sprintf(message, "exec %s", command);
            //     perror(message);
            //     delete[] message;
            //     exit(errno);  // exit so that the function does not return twice!
            //     perror("execve");
            //     _exit(1);
            // }
            // // wait for child termination and retrieve its output
            // else {  // parent
            //     close(pipefd[1]);
            //     char buf[4096];
            //     while (1) {
            //         int n_bytes = readLine(pipefd[0], buf, sizeof(buf));
            //         if (n_bytes == -1) {
            //             if (errno == EINTR) { continue; }
            //             perror("server: readLine");
            //             exit(1);
            //         }
            //         else if (n_bytes == 0 || n_bytes == -2) {  // EOF
            //             break;
            //         }
            //     }
            //     close(pipefd[0]);
            //     waitpid(child, &status, 0);
            // }

            if (strcmp(req, "quit") == 0) {
                strike(sock, &s_sem);  // bye
            }

            FILE* fp;
            char output[4096];
            memset(output, 0, sizeof(output));

            if (strcmp(req, "CPRINT") == 0) {
                if (executed == 0) {
                    echos[echo_id].status = "ERR";
                    echos[echo_id].code = EIO;
                    echos[echo_id].message = "No command has been issued";
                }
                else {
                    while (fgets(output, sizeof(output), fp) != NULL) {  // read shell output from fp
                        send("%s", output);  // send shell output to client
                    }
                }
            }
            else {
                fp = popen(req, "r");  // execute the command in one line, MT-safe
                if (fp == NULL) {
                    echos[echo_id].status = "FAIL";
                    echos[echo_id].code = -4;
                    echos[echo_id].message = "Failed to execute the command";
                }
                else {
                    executed = 1;  // command executed, but may still have an error condition
                    int rc = pclose(fp);  // exit status of the command
                    echos[echo_id].code = rc;
                    if (rc = 0) {
                        echos[echo_id].status = "OK";
                        echos[echo_id].message = "Command executed successfully";
                    }
                    else {
                        echos[echo_id].status = "ERR";
                        echos[echo_id].message = "Command executed with errors";
                    }
                }
            }

            // send response to client
            char res[4096];
            memset(res, 0, sizeof(res));

            sprintf(res, "%s", echos[echo_id].status);
            sprintf(res + strlen(res), " %d", echos[echo_id].code);
            sprintf(res + strlen(res), " %s", echos[echo_id].message);
            int len = strlen(res);
            res[len] = '\n';

            if (sendall(sock, res, &len) == -1) {
                perror("sendall3");  // SIGPIPE already handled in main()
                printf("only %d bytes of data have been sent!\n", len);
                strike(sock, &s_sem);
            }
        }

        else {  // will reach here only if poll() timed out
            char* farewell = "your session has expired";
            int len = strlen(farewell);
            if (sendall(sock, farewell, &len) == -1) {  // say good-bye to client
                perror("sendall4");
                printf("only %d bytes of data have been sent!\n", len);
                strike(sock, &s_sem);
            }

            // close socket
            shutdown(sock, SHUT_WR);
            close(sock);

            // release semaphore and exit thread with status 0
            sem_post(&s_sem);
            pthread_exit((void*)0);
        }
    }
}

void* f_worker(void* csock) {
    // wait semaphore
    sem_wait(&f_sem);

    // obtain an entry in echos[] specified by echo_id, and increment n_echos, MT-safe
    pthread_mutex_lock(mutex);
    int echo_id = n_echo;
    n_echo++;
    pthread_mutex_unlock(mutex);
    echos[echo_id].tid = pthread_self();

    // add client socket to poll()
    int sock = *((int*)csock);
    struct pollfd cfds[1];
    cfds[0].fd = sock;
    cfds[0].events = POLLIN;
    int n_res;

    // repeatedly receive a request from client and handle it
    while ((n_res = poll(cfds, 1, 60000)) != 0) {  // time out after 1 minute of inactivity
        if (n_res < 0) {
            perror("poll");
            strike(sock, &f_sem);  // worker go on strike
        }

        char req[256];
        memset(req, 0, sizeof(req));
        int n_bytes;  // number of bytes received

        // receive a client request
        if (cfds[0].revents & POLLIN) {
            n_bytes = recv(sock, req, sizeof(req) - 1, 0);
            if (n_bytes == 0) {
                printf("connection closed by client on socket %d\n", sock);
                strike(sock, &f_sem);
            }
            else if (n_bytes < 0) {
                perror("recv");
                strike(sock, &f_sem);
            }
        }

        // replace the newline
        if (strlen(req) > 0 && req[strlen(req) - 1] == '\n') {
            req[strlen(req) - 1] = '\0';
        }
        if (VERBOSE_MODE) {
            printf("received client request: %s\n", req);
        }

        // parse client request to obtain argv[]
        char* tokens[strlen(req)];
        char** argv = tokens;
        int argc = tokenize(req, argv, strlen(req));
        argv[argc] = 0;

        // if client just pressed Enter('\n'), start over
        if (strlen(argv[0]) == 0) {
            continue;
        }

        // execute command from client
        int lock_id;  // specify an entry of struct lock_t in locks[]

        if (strcmp(argv[0], "FOPEN") == 0) {
            lock_id = opener(argc, argv, echo_id);  // open the file and assign a lock_id
            if (lock_id < 0) {
                perror("opener");
                strike(sock, &f_sem);
            }
        }
        else if (strcmp(argv[0], "FSEEK") == 0) {
            if ((seeker(argc, argv, echo_id, lock_id)) != 0) {
                perror("seeker");
                strike(sock, &f_sem);
            }
        }
        else if (strcmp(argv[0], "FREAD") == 0) {
            if ((reader(argc, argv, echo_id, lock_id)) != 0) {
                perror("reader");
                strike(sock, &f_sem);
            }
        }
        else if (strcmp(argv[0], "FWRITE") == 0) {
            if ((writer(argc, argv, echo_id, lock_id)) != 0) {
                perror("writer");
                strike(sock, &f_sem);
            }
        }
        else if (strcmp(argv[0], "FCLOSE") == 0) {
            if ((closer(argc, argv, echo_id, lock_id)) != 0) {
                perror("closer");
                strike(sock, &f_sem);
            }
        }
        else if (strcmp(argv[0], "quit") == 0) {
            strike(sock, &f_sem);  // bye
        }
        else {  // invalid command
            echos[echo_id].status = "FAIL";
            echos[echo_id].code = -9;
            echos[echo_id].message = "invalid request";
        }

        // send response to client
        char res[4096];
        memset(res, 0, sizeof(res));

        sprintf(res, "%s", echos[echo_id].status);
        sprintf(res + strlen(res), " %d", echos[echo_id].code);
        sprintf(res + strlen(res), " %s", echos[echo_id].message);
        int len = strlen(res);
        res[len] = '\n';

        if (sendall(sock, res, &len) == -1) {
            perror("sendall");  // SIGPIPE already handled in main()
            printf("only %d bytes of data have been sent!\n", len);
            strike(sock, &f_sem);
        }
    }

    // will reach here only if poll() timed out
    char* farewell = "your session has expired";
    int len = strlen(farewell);
    if (sendall(sock, farewell, &len) == -1) {  // say good-bye to client
        perror("sendall2");
        printf("only %d bytes of data have been sent!\n", len);
        strike(sock, &f_sem);
    }

    // close socket
    shutdown(sock, SHUT_WR);
    close(sock);

    // release semaphore and exit thread
    sem_post(&f_sem);
    pthread_exit((void*)0);
}





int main(int argc, char* argv[]) {
    int DEBUG_MODE = 1;
    int DELAY_MODE = 0;
    int VERBOSE_MODE = 1;
    char* s_port = 10144;  // default shell server port number, echo $((8000 + `id -u`))
    char* f_port = 10145;  // default file server port number, echo $((8001 + `id -u`))

    // tokenize command line switches and arguments
    // ...
    // if (argv[i]) { s_port = argv[i]; }
    // if (argv[j]) { f_port = argv[j]; }

    // startup server
    if (!DEBUG_MODE) {
        // start server in background
        if (fork() == 0) {
            if (init_server() != 0) {
                printf("failed to initialize server (%d)\n");
                exit(1);
            }
        }
        else {
            return 0;
        }
    }
    else {
        // start server in foreground
        if (init_server() != 0) {
            printf("failed to initialize server (%d)\n");
            exit(1);
        }
    }

    // prepare for socket connection
    int ssock, fsock, csock;  // shell master socket, file master socket, client socket
    struct sockaddr_storage cli_addr;
    socklen_t sin_size;
    char ipstr[INET6_ADDRSTRLEN];

    // establish master sockets
    ssock = setListener(NULL, s_port, 128);
    fsock = setListener(NULL, f_port, 128);
    if (ssock == -1 || fsock == -1) {
        printf("unable to establish a listener socket\n");
        exit(2);
    }

    // add master sockets to poll()
    struct pollfd pfds[2];
    pfds[0] = { .fd = ssock, .events = POLLIN };
    pfds[1] = { .fd = fsock, .events = POLLIN };

    // launch the monitor thread
    // pthread_t mid;
    // if (pthread_create(&mid, &attr, monitor, NULL) != 0) {
    //     perror("pthread_create");
    //     exit(3);
    // }

    // prepare arrays for holding threads
    int s = 0;  // initial thread index
    int f = 0;  // initial thread index
    pthread_t sid[N_THREADS];  // 128 total threads for shell service, not reusable
    pthread_t fid[N_THREADS];  // 128 total threads for file service, not reusable
    // (if we want to dynamically create reusable slots for threads, we need a thread pool class)

    // accept incoming clients
    while (1) {
        int n_res = poll(pfds, 2, -1);  // timeout = -1, infinite poll
        if (n_res == -1) {
            perror("poll");
            exit(4);
        }

        for (int i = 0; i < 2; i++) {
            if (pfds[i].revents & POLLIN) {
                sin_size = sizeof(cli_addr);
                csock = accept(pfds[i].fd, (struct sockaddr*)&cli_addr, &sin_size);
                if (csock == -1) {
                    perror("accept");
                    exit(5);
                }

                // new client connected
                inet_ntop(cli_addr.ss_family, getInAddr((struct sockaddr*)&cli_addr), ipstr, INET6_ADDRSTRLEN);
                printf("new connection from %s on socket %d\n", ipstr, csock);

                if (pfds[i].fd == ssock) {
                    // invoke a new shell worker thread
                    if (pthread_create(&sid[s], &attr, s_worker, (void*)csock) != 0) {
                        perror("pthread_create");
                        exit(6);
                    }
                    s++;
                }
                else {
                    // invoke a new file worker thread
                    if (pthread_create(&fid[f], &attr, f_worker, (void*)csock) != 0) {
                        perror("pthread_create");
                        exit(7);
                    }
                    f++;
                }
            }
        }
    }

    // will never reach this apocalypse
    exit_server();
    return EXIT_SUCCESS;
}
