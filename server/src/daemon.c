/*
** daemon.c -- daemon server utilities, automatic threads management, dynamic reconfiguration, etc.
*/

#include "define.h"

void logger(const char* message) {
    pthread_mutex_lock(&logger_mutex);
    time_t now = time(0);
    struct tm* timestamp = localtime(&now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timestamp);
    printf("%s  %s\n", buf, message);
    fflush(stdout);
    pthread_mutex_unlock(&logger_mutex);
}

void* monitor_thread(void* omitted) {
    // initialize the thread pool
    thread_pool_size = monitor.t_max + monitor.t_inc;
    thread_pool = (struct thread_t*)malloc(sizeof(struct thread_t) * thread_pool_size);
    for (int i = 0; i < thread_pool_size; i++) {
        thread_pool[i].tid = -1;
        thread_pool[i].idle = 1;
    }

    // preallocate a batch of t_inc threads
    for (int i = 0; i < monitor.t_inc; i++) {
        if (pthread_create(&thread_pool[i].tid, &attr, file_thread, (void*)(intptr_t)i) != 0) {
            perror("pthread_create");
            fflush(stderr);
            exit(76);
        }
    }
    pthread_mutex_lock(&monitor.m_mtx);
    monitor.t_tot += monitor.t_inc;
    pthread_mutex_unlock(&monitor.m_mtx);

    while (1) {
        // check if all threads have been used up (no busy loop)
        pthread_mutex_lock(&monitor.m_mtx);
        while (monitor.t_act < monitor.t_tot || monitor.t_tot >= monitor.t_max) {
            pthread_cond_wait(&monitor.m_cond, &monitor.m_mtx);
        }

        // if master socket temporarily closed by dynamic reconfiguration, just skip
        if (fsock == -1) {
            pthread_mutex_unlock(&monitor.m_mtx);
            continue;
        }

        // now we preallocate another batch of t_inc threads
        for (int i = 0; i < monitor.t_inc; i++) {
            if (pthread_create(&thread_pool[monitor.t_tot + i].tid, &attr, file_thread, (void*)(intptr_t)(monitor.t_tot + i)) != 0) {
                perror("pthread_create");
                fflush(stderr);
                exit(176);
            }
        }
        monitor.t_tot += monitor.t_inc;
        pthread_mutex_unlock(&monitor.m_mtx);
    }
}

int free_server(void) {
    // first close the master socket so that no new clients will be accepted
    logger("(free_server): temporarily closing master socket...");
    fsock_tmp = fsock;
    fsock = -1;
    sleep(2);

    // then, all the client threads will eventually quit on their own, so we just sit and wait
    // this is because, idle threads blocking on accept will fail due to closed master socket
    // similarly, busy threads will eventually become idle and fail on accept as well
    // failed accept has been properly handled at fserv.c on line 554, so that threads exit normally
    logger("(free_server): waiting for busy clients...");
    for (int i = 0; i < thread_pool_size; i++) {
        if (thread_pool[i].tid == -1) continue;  // skip unused entry
        while (thread_pool[i].idle == 0) {  // some thread is still busy
            sleep(1);  // check again after 1 second, no busy loop
        }
    }

    // if we reach here, all threads have quit
    logger("(free_server): resetting threads usage...");
    monitor.t_act = 0;
    monitor.t_tot = 0;

    // reset the locks array (close all file descriptors opened by clients)
    logger("(free_server): cleaning up opened files...");
    for (int lock_id = 0; lock_id < n_lock; lock_id++) {
        reset_lock(lock_id);
    }
    n_lock = 0;

    // finally, free thread pool memory
    logger("(free_server): freeing allocated thread memory...");
    free(thread_pool);
    return 0;
}

int stop_server(void) {
    // free up server resources
    if (free_server() != 0) {
        logger("failed to free up resources");
        exit(-7);
    }

    // destroy global mutexes, thread attribute
    logger("(stop_server): destroying locks and mutexes...");
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&wake_mutex);
    pthread_mutex_destroy(&lock_mutex);

    // unlock, close and unlink server's lock file
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    logger("(stop_server): releasing server's lock file...");
    if (fcntl(lockfile, F_SETLK, &fl) == -1) {
        perror("unlock log file");
        fflush(stderr);
        exit(1);
    }

    logger("(stop_server): server termination complete!\n");
    pthread_mutex_destroy(&logger_mutex);  // after the last log message
    close(lockfile);
    // unlink("./shfd.log");  // should not delete file
    exit(0);  // terminate server
}

int reset_server(void) {
    // free up server resources
    if (free_server() != 0) {
        logger("failed to free up resources");
        exit(-23);
    }

    // re-establish the master socket
    logger("(reset_server): re-establishing master socket connection...");
    fsock = fsock_tmp;

    // reset thread pool and preallocate a batch of threads
    logger("(reset_server): re-allocating thread pool...");
    thread_pool = (struct thread_t*)malloc(sizeof(struct thread_t) * thread_pool_size);
    for (int i = 0; i < thread_pool_size; i++) {
        thread_pool[i].tid = -1;
        thread_pool[i].idle = 1;
    }
    for (int i = 0; i < monitor.t_inc; i++) {
        if (pthread_create(&thread_pool[i].tid, &attr, file_thread, (void*)(intptr_t)i) != 0) {
            perror("pthread_create");
            fflush(stderr);
            exit(76);
        }
    }
    pthread_mutex_lock(&monitor.m_mtx);
    monitor.t_tot += monitor.t_inc;
    pthread_mutex_unlock(&monitor.m_mtx);

    logger("(reset_server): server reloading complete!\n");
    return 0;
}

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
    umask(0177);

    return lf;
}

int init_server() {
    // initialize mutex, condition variable, thread attribute
    pthread_mutex_init(&lock_mutex, NULL);
    pthread_mutex_init(&wake_mutex, NULL);
    pthread_mutex_init(&logger_mutex, NULL);
    pthread_attr_init(&attr);
    size_t stacksize = sizeof(double) * N * N + MEGEXTRA;
    pthread_attr_setstacksize(&attr, stacksize);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // start the daemon
    if (!DEBUG_MODE) {
        int pid = fork();
        if (pid == 0) {
            if ((lockfile = daemonize()) != 1) {  // start daemon server in background
                printf("failed to daemonize server\n");
                return 1;
            }
        }
        else {
            exit(0);
        }
    }
    else {
        return 0;  // start normal server in foreground
    }
}
