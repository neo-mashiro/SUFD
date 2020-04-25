#ifndef _DEFINE_H
#define _DEFINE_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "./utils.h"

#define N 1000
#define MEGEXTRA 1000000

extern int lockfile;  // server's log file (to be locked)

extern int DEBUG_MODE;
extern int DELAY_MODE;
extern int VERBOSE_MODE;

extern int ssock;  // shell master socket
extern int fsock;  // file master socket
extern int fsock_tmp;  // store fsock's old value when it's temporarily down for reconfiguration

extern char* s_port;  // shell port number
extern char* f_port;  // file port number

extern char* peers[64];  // an array of [host:port] pairs for the replication servers

extern pthread_attr_t attr;
extern pthread_mutex_t wake_mutex;
extern pthread_mutex_t lock_mutex;
extern pthread_mutex_t logger_mutex;

struct thread_t {
    pthread_t tid;
    int idle;  // 1 = idle, 0 = busy
};

extern int thread_pool_size;
extern struct thread_t* thread_pool;  // pointer to global thread pool

struct monitor_t {  // for dynamic threads management based on network traffic
    int t_inc;      // number of pre-allocated threads
    int t_act;      // number of active threads
    int t_tot;      // total number of allocated threads
    int t_max;      // maximum capacity
    pthread_mutex_t m_mtx;
    pthread_cond_t m_cond;
} extern monitor;

struct echo_t {     // for server response
    char* status;   // OK / FAIL / ERR
    int code;       // server side error code
    char* message;  // client-friendly message
};

struct lock_t {               // for CREW file access control
    pthread_mutex_t f_mtx;    // file access mutex
    pthread_cond_t f_cond;    // file access condition variable
    char f_name[256];         // file name (path)
    int fd;                   // file descriptor (identifier)
    unsigned short n_reader;  // number of readers
    unsigned short n_writer;  // number of writers, 0 or 1, at most 1
};

extern struct lock_t locks[65535];  // each file is associated with a unique lock entry
extern int n_lock;  // number of lock entries used

void logger(const char* message);

int init_server(void);

int stop_server(void);

int reset_server(void);

void serve_admin(int asock);

void reset_lock(int lock_id);

void* file_thread(void* fsock);

void* signal_thread(void* set);

void* monitor_thread(void* omitted);

#endif
