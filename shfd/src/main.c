/*
** shfd -- Assignment 3 (CS 464/564), Bishop's University
**
** @author:    Wentao Lu (002276355), Yi Ren (002212345)
** @date:      2020/03/21
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
#include <sys/ioctl.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <semaphore.h>

#include "utils.h"

#define N_THREADS 128
#define N 1000
#define MEGEXTRA 1000000

int lockfile;  // server's log file (to be locked)
int DEBUG_MODE = 0;
int DELAY_MODE = 0;
int VERBOSE_MODE = 0;
char* s_port = "9001";  // default shell service port number, use echo $((8000 + `id -u`)) = 10144 on linux.bishops.ca
char* f_port = "9002";  // default file service port number, use echo $((8001 + `id -u`)) = 10145 on linux.bishops.ca
const char* prompt = "> ";
const char* path[] = {"/bin", "/usr/bin", 0};

struct monitor_t {          // struct for monitor data
    pthread_mutex_t m_mtx;  // monitor mutex
    int num_conn;           // number of active connections
    int max_conn;           // highest historical number of connections
    int tot_conn;           // total number of connections served
    int tot_time;           // total processing time
} stats;

struct lock_t {               // struct for file access control
    char f_name[256];         // file name (path)
    int fd;                   // file descriptor (identifier)
    pthread_mutex_t f_mtx;    // file access mutex
    pthread_cond_t f_cond;    // file access condition variable
    unsigned short n_reader;  // number of readers
    unsigned short n_writer;  // number of writers, 0 or 1, at most 1
};

struct echo_t {           // struct for server response
    pthread_t tid;        // unique thread id
    char* status;   // OK / FAIL / ERR
    int code;             // server side error code
    char* message;        // client-friendly message
};

struct echo_t echos[1024];  // every thread (client) receives a server response
struct lock_t locks[1024];  // every filename is associated with a unique lock
int n_echo = 0;  // number of echos used
int n_lock = 0;  // number of locks used

sem_t s_sem;  // semaphore for limiting the number of shell threads
sem_t f_sem;  // semaphore for limiting the number of file threads
pthread_attr_t attr;  // universal thread attribute
pthread_mutex_t mutex;  // mutex for safely updating global counts (n_echo and n_lock)

int daemonize(void) {
    // close stdin 0, stdout 1, stderr 2 and everything
    for (int i = getdtablesize() - 1; i >= 0 ; i--) {
        close(i);
    }

    // redirect 0 to "/dev/null", 1 and 2 to server's log file
    int lf = open("/dev/null", O_RDWR);  // after we closed 0,1,2, now "/dev/null" will be opened on 0 (stdin)
    lf = open("./shfd.log", O_WRONLY|O_CREAT|O_APPEND, 0640);  // open server's log file on 1 (stdout)
    if (lf == -1 || lf > 1) {
        perror("open server log file");
        exit(-1);
    }
    dup(lf);  // redirect stderr to the same server log file

    // lock server's log file to enforce only 1 running copy of the server
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;  // lock to EOF

    if (fcntl(lf, F_SETLK, &fl) == -1) {  // POSIX record locks suffice to ensure mutual exclusion among distinct processes
        printf("another copy of the server is already running, exiting pid %d...\n", getpid());
        exit(-1);
    }
    // if (flock(fd, LOCK_EX|LOCK_NB)) {  // BSD locks also work
    //     printf("another copy of the server is already running, exiting...");
    //     exit(0);
    // }

    // save server's process id in the log file
    char pbuf[50];
    sprintf(pbuf, "daemon started, process id is %d\n", getpid());
    write(lf, pbuf, strlen(pbuf));

    // detach the server from tty, so that it will not receive signals from its parent (the init process)
    int fd = open("/dev/tty", O_RDWR);
    ioctl(fd, TIOCNOTTY, 0);
    close(fd);

    // move server to a safe directory
    chdir("./run");

    // place server into a single process group
    if (setpgid(getpid(),0) != 0) {
        perror("setpgid");
        fflush(stderr);
        exit(-1);
    }

    // set umask
    umask(027);

    // ignore all signals
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = SIG_IGN;

    for (int s = 1; s < 65 ; s++) {
        if((s != SIGKILL) && (s != SIGSTOP) && (s != 32) && (s != 33)) {
            if (sigaction(s, &sa, NULL) == -1) {
                perror("sigaction");
                fflush(stderr);
                exit(1);
            }
        }
    }

    return lf;
}

int init_server() {
    // initialize semaphore, mutex, condition variable, thread attribute
    sem_init(&s_sem, 0, N_THREADS);
    sem_init(&f_sem, 0, N_THREADS);
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&stats.m_mtx, NULL);
    pthread_attr_init(&attr);
    size_t stacksize = sizeof(double) * N * N + MEGEXTRA;
    pthread_attr_setstacksize(&attr, stacksize);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);  // every client is independent

    // set round-robin schedule, need root access
    // struct sched_param param;
    // memset(&param, 0, sizeof(param));
    // param.sched_priority = 1;
    // if (sched_setscheduler(0, SCHED_RR, &param) != 0) {
    //     perror("sched_setscheduler");
    //     fflush(stderr);
    //     exit(1);
    // }
    return 0;
}

int exit_server() {
    // release resources
    sem_destroy(&s_sem);
    sem_destroy(&f_sem);
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&stats.m_mtx);

    for (int i = 0; i < n_lock; i++) {
        unlink(locks[i].f_name);
        close(locks[i].fd);
        pthread_mutex_destroy(&locks[i].f_mtx);
        pthread_cond_destroy(&locks[i].f_cond);
    }

    // unlock server's log file
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(lockfile, F_SETLK, &fl) == -1) {
        perror("unlock log file");
        fflush(stderr);
        exit(1);
    }

    // release the log file and exit the main thread
    close(lockfile);
    unlink("/shfd.log");
    pthread_exit(NULL);
}

int opener(int argc, char** argv, int echo_id) {
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

    if (fcntl(fd, F_OFD_SETLK, &fl) == -1) {  // open file description locks for synchronization among threads
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
    pthread_mutex_lock(&mutex);
    int lock_id = n_lock;
    n_lock++;
    pthread_mutex_unlock(&mutex);

    // update shared struct lock_t, prepare file for future manipulation
    memset(&locks[lock_id].f_name, 0, sizeof(locks[lock_id].f_name));
    strcpy(locks[lock_id].f_name, filename);  // don't use char* !!!!
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

int seeker(int argc, char** argv, int echo_id, int lock_id) {
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

    int identifier = atoi(argv[1]);
    off_t offset = atoi(argv[2]);  // offset can be negative!
    struct lock_t* lock = &locks[lock_id];

    if (identifier != lock->fd || lock->fd <= 0) {
        echos[echo_id].status = "ERR";
        echos[echo_id].code = ENOENT;
        echos[echo_id].message = "invalid identifier, no such file or directory";
        return 0;
    }

    // waiting for resources
    pthread_mutex_lock(&lock->f_mtx);
    while (lock->n_writer > 0) {
        pthread_cond_wait(&lock->f_cond, &lock->f_mtx);
    }
    lock->n_reader++;  // seeking is the same as reading
    pthread_mutex_unlock(&lock->f_mtx);

    // seeking... all threads share one fd and one seek pointer
    int pos = lseek(identifier, offset, SEEK_CUR);  // position of the seek pointer
    if (pos == -1) {  // lseek() is atomic just like read() and write()
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = errno;
        echos[echo_id].message = "system call lseek() returns -1";
        return 0;
    }

    // seeking finished
    pthread_mutex_lock(&lock->f_mtx);
    lock->n_reader--;
    pthread_cond_broadcast(&lock->f_cond);
    pthread_mutex_unlock(&lock->f_mtx);

    // success response
    echos[echo_id].status = "OK";
    echos[echo_id].code = 0;
    char temp[100];
    memset(temp, 0, sizeof(temp));
    sprintf(temp, "seek pointer is now %d bytes from the beginning of the file", pos);
    echos[echo_id].message = temp;

    return 0;
}

int reader(int argc, char** argv, int echo_id, int lock_id) {
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

    int identifier = atoi(argv[1]);
    int len = atoi(argv[2]);
    struct lock_t* lock = &locks[lock_id];

    if (len < 0) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = -6;
        echos[echo_id].message = "invalid length value";
        return 0;
    }

    if (identifier != lock->fd || lock->fd <= 0) {
        echos[echo_id].status = "ERR";
        echos[echo_id].code = ENOENT;
        echos[echo_id].message = "invalid identifier, no such file or directory";
        return 0;
    }

    // waiting for resources
    pthread_mutex_lock(&lock->f_mtx);
    while (lock->n_writer > 0) {
        pthread_cond_wait(&lock->f_cond, &lock->f_mtx);
    }
    lock->n_reader++;
    pthread_mutex_unlock(&lock->f_mtx);

    // reading...
    if (DELAY_MODE) {
        printf("client session %d starts reading %s...\n", echo_id, lock->f_name);
        fflush(stdout);
        sleep(3);
    }
    char buf[4096];
    memset(buf, 0, sizeof(buf));
    int n = read(lock->fd, buf, len);
    if (n == -1) {
        echos[echo_id].status = "FAIL";
        echos[echo_id].code = errno;
        echos[echo_id].message = "system call read() returns -1";
        return 0;
    }
    if (buf[strlen(buf) - 1] == '\n') {
        buf[strlen(buf) - 1] = '\0';
    }
    if (DELAY_MODE) {
        printf("client session %d finished reading %s ...\n", echo_id, lock->f_name);
        fflush(stdout);
    }

    // reading finished
    pthread_mutex_lock(&lock->f_mtx);
    lock->n_reader--;
    pthread_cond_broadcast(&lock->f_cond);
    pthread_mutex_unlock(&lock->f_mtx);

    // success response
    echos[echo_id].status = "OK";
    echos[echo_id].code = n;
    echos[echo_id].message = buf;

    return 0;
}

int writer(int argc, char** argv, int echo_id, int lock_id) {
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

    int identifier = atoi(argv[1]);
    char* buf = argv[2];

    struct lock_t* lock = &locks[lock_id];

    if (identifier != lock->fd || lock->fd <= 0) {
        echos[echo_id].status = "ERR";
        echos[echo_id].code = ENOENT;
        echos[echo_id].message = "invalid identifier, no such file or directory";
        return 0;
    }

    // waiting for resources
    pthread_mutex_lock(&lock->f_mtx);
    while (lock->n_reader > 0 || lock->n_writer > 0) {
        pthread_cond_wait(&lock->f_cond, &lock->f_mtx);
    }
    lock->n_writer++;
    pthread_mutex_unlock(&lock->f_mtx);

    // writing...
    if (DELAY_MODE) {
        printf("client session %d starts writing %s ...\n", echo_id, lock->f_name);
        fflush(stdout);
        sleep(6);
    }
    int len = strlen(buf);
    int total = 0;   // bytes sent
    int left = len;  // bytes left
    int n;
    while (total < left) {
        n = write(lock->fd, buf + total, left);  // update seek
        if (n == -1) {
            echos[echo_id].status = "FAIL";
            echos[echo_id].code = errno;
            echos[echo_id].message = "system call write() returns -1";
            return 0;
        }
        total += n;
        left -= n;
    }
    if (DELAY_MODE) {
        printf("client session %d finished writing %s ...\n", echo_id, lock->f_name);
        fflush(stdout);
    }

    // writing finished
    pthread_mutex_lock(&lock->f_mtx);
    lock->n_writer--;
    pthread_cond_broadcast(&lock->f_cond);
    pthread_mutex_unlock(&lock->f_mtx);

    // success response
    echos[echo_id].status = "OK";
    echos[echo_id].code = 0;
    char temp[100];
    memset(temp, 0, sizeof(temp));
    sprintf(temp, "wrote %d bytes data to the file", total);
    echos[echo_id].message = temp;

    return 0;
}

int closer(int argc, char** argv, int echo_id, int lock_id) {
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

    int identifier = atoi(argv[1]);
    struct lock_t* lock = &locks[lock_id];

    if (identifier != lock->fd || lock->fd <= 0) {
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

    if (fcntl(identifier, F_OFD_SETLK, &fl) == -1) {  // unlock file
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

    // upon close() success, must reset the locks[lock_id] entry to avoid corrupt behavior in other threads
    locks[lock_id].fd = 0;
    memset(&locks[lock_id].f_name, 0, sizeof(locks[lock_id].f_name));

    // success response
    echos[echo_id].status = "OK";
    echos[echo_id].code = 0;
    echos[echo_id].message = "file closed";

    // upon success of a close() command, we should also safely decrement n_lock
    // and remove the corresponding entry from locks[]. Unfortunately, it's hard
    // to do so in C with merely a static-allocated C-array given the complexity
    // of a multithreading synchronization context. It would be much nicer if dynamic
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

void* monitor(void* omitted) {
    while (1) {
        sleep(1200);  // report server statistics every 2 minutes
        pthread_mutex_lock(&stats.m_mtx);
        time_t now = time(0);  // create a timer pointer
        printf("monitor: %s\n", ctime(&now));  // local calendar time
        printf("monitor: total number of connections served: %d\n", stats.tot_conn);
        printf("monitor: number of currently active connections: %d\n", stats.num_conn);
        printf("monitor: highest historical number of connections: %d\n", stats.max_conn);
        printf("monitor: average connection time: %d seconds\n", (int)((float)stats.tot_time/(float)(stats.tot_conn + 1)));
        pthread_mutex_unlock(&stats.m_mtx);
    }
    pthread_exit(NULL);
}

void* s_worker(void* csock) {
    // wait semaphore
    sem_wait(&s_sem);

    // obtain an entry in echos[] specified by echo_id, and increment n_echos, MT-safe
    pthread_mutex_lock(&mutex);
    int echo_id = n_echo;
    n_echo++;
    pthread_mutex_unlock(&mutex);
    echos[echo_id].tid = pthread_self();

    // add client socket to poll()
    int sock = (int)(intptr_t)csock;
    struct pollfd cfds[1];
    cfds[0].fd = sock;
    cfds[0].events = POLLIN;
    int n_res;

    // prepare to monitor shell command execution in this session
    int status = 0;     // catch a child process exit status
    int executed = 0;   // check if a shell command has been issued
    char output[4096];  // buffer to store the shell command output
    int pipefd[2];      // create a pipe for IPC
    if (pipe(pipefd) == -1) {
        perror("pipe");
        fflush(stderr);
        strike(sock, &s_sem);
    }

    // repeatedly receive a client command and handle it
    while (1) {
        if (send(sock, prompt, strlen(prompt), 0) < 0) {
            if (errno == EPIPE) {
                printf("connection closed by client on socket %d\n", sock);
                fflush(stdout);
            }
            perror("send");
            fflush(stderr);
            strike(sock, &s_sem);
        }

        if ((n_res = poll(cfds, 1, 300000)) != 0) {  // time out after 5 minutes of inactivity
            if (n_res < 0) {
                perror("poll");
                fflush(stderr);
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
                    fflush(stdout);
                    strike(sock, &s_sem);
                }
                else if (n_bytes < 0) {
                    perror("recv");
                    fflush(stderr);
                    strike(sock, &s_sem);
                }
            }

            // replace the newline
            int slen = strlen(req);
            if (slen > 0 && req[slen - 1] == '\n') {
                req[slen - 1] = '\0';
                if (req[slen - 2] == '\r') req[slen - 2] = '\0';  // windows CRLF \r\n
            }
            if (VERBOSE_MODE) {
                printf("received client request: %s\n", req);
                fflush(stdout);
            }

            // if client wants to quit, exit thread
            if (strcmp(req, "quit") == 0) {
                printf("closing client connection on socket %d\n", sock);
                fflush(stdout);
                strike(sock, &s_sem);  // bye
            }

            // parse client request to obtain argv[]
            char* envp[] = { NULL };
            char* tokens[strlen(req)];
            char** argv = tokens;
            int argc = tokenize(req, argv, strlen(req));
            argv[argc] = 0;

            // if client just pressed Enter('\n'), start over
            if (strlen(argv[0]) == 0) {
                continue;
            }

            // now execute client command
            if (strcmp(argv[0], "CPRINT") == 0) {
                // send shell output to client
                if (executed == 0) {
                    echos[echo_id].status = "ERR";
                    echos[echo_id].code = EIO;
                    echos[echo_id].message = "No command has been issued";
                }
                else {
                    if (send(sock, output, strlen(output), 0) < 0) {
                        perror("send");
                        fflush(stderr);
                        echos[echo_id].status = "FAIL";
                        echos[echo_id].code = -7;
                        echos[echo_id].message = "Failed to send output";
                    }
                    else {
                        echos[echo_id].status = "OK";
                        echos[echo_id].code = 0;
                        echos[echo_id].message = "Last executed shell output sent";
                    }
                }
            }
            else {
                pid_t child = fork();

                // child exec() the shell command
                if (child == 0) {
                    close(1);  // stdout
                    close(2);  // stderr
                    dup(pipefd[1]);  // redirect child's STDOUT to pipefd[1]
                    dup(pipefd[1]);  // redirect child's STDERR to pipefd[1]
                    close(pipefd[0]);
                    execve(argv[0], argv, envp);  // attempt to execute with no path prefix ...
                    for (size_t i = 0; path[i] != 0; i++) {  // then try with path prefixed
                        char cp[256];
                        memset(cp, 0, sizeof(cp));
                        sprintf(cp, "%s/%s", path[i], argv[0]);
                        execve(cp, argv, envp);
                    }
                    perror("execve");
                    _exit(-1);  // exit child if execve() failed
                }

                // parent wait() for child's output
                else {
                    memset(output, 0, sizeof(output));  // flush output buffer
                    while (1) {
                        int n_bytes = readtimeout(pipefd[0], output, sizeof(output), 100);  // non-block read()
                        if (n_bytes == -1) {
                            if (errno == EINTR) { continue; }
                            perror("readtimeout");
                            fflush(stderr);
                            strike(sock, &s_sem);
                        }
                        else if (n_bytes == 0 || n_bytes == -2) {  // EOF, no data in the pipe
                            break;
                        }
                    }
                    waitpid(child, &status, 0);
                }

                // only parent continues
                if (strlen(output) == 0) {
                    echos[echo_id].status = "FAIL";
                    echos[echo_id].code = -4;
                    echos[echo_id].message = "Failed to execute the command";
                }
                else {
                    executed = 1;  // command executed, but may still have an error condition
                    echos[echo_id].code = status;
                    if (status == 0) {
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
            len++;

            if (sendall(sock, res, &len) == -1) {
                perror("sendall3");  // SIGPIPE already handled in main()
                printf("only %d bytes of data have been sent!\n", len);
                fflush(stdout); fflush(stderr);
                strike(sock, &s_sem);
            }
        }

        else {  // will reach here only if poll() timed out
            const char* farewell = "your session has expired\n";
            int len = strlen(farewell);
            if (sendall(sock, farewell, &len) == -1) {  // say good-bye to client
                perror("sendall4");
                printf("only %d bytes of data have been sent!\n", len);
                fflush(stdout); fflush(stderr);
                strike(sock, &s_sem);
            }

            printf("closing client connection on socket %d\n", sock);
            fflush(stdout);

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
    pthread_mutex_lock(&mutex);
    int echo_id = n_echo;
    n_echo++;
    pthread_mutex_unlock(&mutex);
    echos[echo_id].tid = pthread_self();

    // add client socket to poll()
    int sock = (int)(intptr_t)csock;
    struct pollfd cfds[1];
    cfds[0].fd = sock;
    cfds[0].events = POLLIN;
    int n_res;

    int lock_id;  // specify an entry of struct lock_t in locks[]

    // repeatedly receive a request from client and handle it
    while (1) {
        if (send(sock, prompt, strlen(prompt), 0) < 0) {
            if (errno == EPIPE) {
                printf("connection closed by client on socket %d\n", sock);
                fflush(stdout);
            }
            perror("send");
            fflush(stderr);
            strike(sock, &f_sem);
        }

        if ((n_res = poll(cfds, 1, 300000)) != 0) {  // time out after 5 minutes of inactivity
            if (n_res < 0) {
                perror("poll");
                fflush(stderr);
                strike(sock, &f_sem);  // worker go on strike
            }

            char req[256];
            memset(req, 0, sizeof(req));
            int n_bytes = 0;  // number of bytes received

            // receive a client request
            if (cfds[0].revents & POLLIN) {
                n_bytes = recv(sock, req, sizeof(req) - 1, 0);
                if (n_bytes == 0) {
                    printf("connection closed by client on socket %d\n", sock);
                    fflush(stdout);
                    strike(sock, &f_sem);
                }
                else if (n_bytes < 0) {
                    perror("recv");
                    fflush(stderr);
                    strike(sock, &f_sem);
                }
            }

            // replace the newline
            int slen = strlen(req);
            if (slen > 0 && req[slen - 1] == '\n') {
                req[slen - 1] = '\0';
                if (req[slen - 2] == '\r') req[slen - 2] = '\0';  // windows CRLF \r\n
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
            if (strcmp(argv[0], "FOPEN") == 0) {
                lock_id = opener(argc, argv, echo_id);  // open the file and assign a lock_id
                if (lock_id < 0) {
                    perror("opener");
                    fflush(stderr);
                    strike(sock, &f_sem);
                }
            }
            else if (strcmp(argv[0], "FSEEK") == 0) {
                if ((seeker(argc, argv, echo_id, lock_id)) != 0) {
                    perror("seeker");
                    fflush(stderr);
                    strike(sock, &f_sem);
                }
            }
            else if (strcmp(argv[0], "FREAD") == 0) {
                if ((reader(argc, argv, echo_id, lock_id)) != 0) {
                    perror("reader");
                    fflush(stderr);
                    strike(sock, &f_sem);
                }
            }
            else if (strcmp(argv[0], "FWRITE") == 0) {
                if ((writer(argc, argv, echo_id, lock_id)) != 0) {
                    perror("writer");
                    fflush(stderr);
                    strike(sock, &f_sem);
                }
            }
            else if (strcmp(argv[0], "FCLOSE") == 0) {
                if ((closer(argc, argv, echo_id, lock_id)) != 0) {
                    perror("closer");
                    fflush(stderr);
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
            len++;

            if (sendall(sock, res, &len) == -1) {
                perror("sendall");  // SIGPIPE already handled in main()
                printf("only %d bytes of data have been sent!\n", len);
                fflush(stdout); fflush(stderr);
                strike(sock, &f_sem);
            }
        }
        else {  // will reach here only if poll() timed out
            const char* farewell = "your session has expired\n";
            int len = strlen(farewell);
            if (sendall(sock, farewell, &len) == -1) {  // say good-bye to client
                perror("sendall2");
                printf("only %d bytes of data have been sent!\n", len);
                fflush(stdout); fflush(stderr);
                strike(sock, &f_sem);
            }

            printf("closing client connection on socket %d\n", sock);
            fflush(stdout);

            // close socket
            shutdown(sock, SHUT_WR);
            close(sock);

            // release semaphore and exit thread with status 0
            sem_post(&f_sem);
            pthread_exit((void*)0);
        }
    }
}

int main(int argc, char* argv[]) {
    // parse command line switches and arguments
    int copt = 0, err_switch = 0;
    while ((copt = getopt(argc, argv, "dvDf:s:")) != -1) {
        char c = (char)copt;
        switch(c) {
            case 'd':
                DEBUG_MODE = 1;
                break;
            case 'D':
                DELAY_MODE = 1;
                break;
            case 'v':
                VERBOSE_MODE = 1;
                break;
            case 's':
                s_port = optarg;
                if (atoi(optarg) == 0) err_switch = 1;
                break;
            case 'f':
                f_port = optarg;
                if (atoi(optarg) == 0) err_switch = 1;
                break;
            case '?':
                err_switch = 1;
                break;
        }
    }
    if (err_switch) {
        fprintf(stderr, "Usage: %s [-d] [-D] [-s port_number] [-f port_number] [-v]\n", argv[0]);
    }

    // startup server
    if (!DEBUG_MODE) {
        // start daemon server in background
        int pid = fork();
        if (pid == 0) {
            if ((lockfile = daemonize()) != 1) {
                printf("failed to daemonize server\n");
                exit(1);
            }
            if (init_server() != 0) {
                printf("failed to initialize server\n");
                fflush(stdout);  // now the log file is opened, each write operation must be followed by a fflush()
                exit(1);
            }
        }
        else {
            return 0;
        }
    }
    else {
        // start normal server in foreground
        if (init_server() != 0) {
            printf("failed to initialize server\n");
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
        fflush(stdout);
        exit(2);
    }

    // add master sockets to poll()
    struct pollfd pfds[2];
    pfds[0].fd = ssock;
    pfds[0].events = POLLIN;
    pfds[1].fd = fsock;
    pfds[1].events = POLLIN;

    // launch the monitor thread
    // pthread_t mid;
    // if (pthread_create(&mid, &attr, monitor, NULL) != 0) {
    //     perror("pthread_create");
    //     fflush(stderr);
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
            fflush(stderr);
            exit(4);
        }

        for (int i = 0; i < 2; i++) {
            if (pfds[i].revents & POLLIN) {
                sin_size = sizeof(cli_addr);
                csock = accept(pfds[i].fd, (struct sockaddr*)&cli_addr, &sin_size);
                if (csock == -1) {
                    perror("accept");
                    fflush(stderr);
                    exit(5);
                }

                // new client connected
                inet_ntop(cli_addr.ss_family, getInAddr((struct sockaddr*)&cli_addr), ipstr, INET6_ADDRSTRLEN);
                printf("new connection from %s on socket %d\n", ipstr, csock);
                fflush(stdout);

                if (pfds[i].fd == ssock) {
                    // invoke a new shell worker thread
                    if (pthread_create(&sid[s], &attr, s_worker, (void*)(intptr_t)csock) != 0) {
                        perror("pthread_create");
                        fflush(stderr);
                        exit(6);
                    }
                    s++;
                }
                else {
                    // invoke a new file worker thread
                    if (pthread_create(&fid[f], &attr, f_worker, (void*)(intptr_t)csock) != 0) {
                        perror("pthread_create");
                        fflush(stderr);
                        exit(7);
                    }
                    f++;
                }
            }
        }
    }

    // unreachable code, but just in case
    exit_server();
    return EXIT_SUCCESS;
}
