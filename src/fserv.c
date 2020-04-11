/*
** fserv.c -- database file service
*/

#include "define.h"

int opener(int argc, char** argv, struct echo_t* echo, int* lock_id, char** file_ptr) {
    // validate request format
    if (argc != 2) {
        echo->status = "FAIL";
        echo->code = -1;
        echo->message = "Usage: FOPEN filename";
        return -1;
    }
    *file_ptr = strdup(argv[1]);
    char* filename = *file_ptr;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;  // lock to EOF

    int fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);  // if file already opened by another thread, we get a new fd for the same file
    if (fd < 0) {
        echo->status = "FAIL";
        echo->code = errno;
        echo->message = "cannot open file";
        return -1;
    }

    if (fcntl(fd, F_OFD_SETLK, &fl) == -1) {  // OFD (open file description) locks are mutual exclusive among threads
        close(fd);  // when file already opened, we just close this new fd and use the previously opened fd
        echo->status = "ERR";
        echo->message = "file already opened";
        // find identifier of the already opened file
        for (int i = 0; i < n_lock; i++) {
            if (strcmp(locks[i].f_name, filename) == 0) {
                echo->code = locks[i].fd;  // identifier
                *lock_id = i;
                return -1;
            }
        }
    }

    // if we reach this, file is opened for the 1st time, assign it a new lock_id, and increment n_lock
    int new_lock_id = n_lock;
    *lock_id = new_lock_id;
    pthread_mutex_lock(&lock_mutex);
    n_lock++;
    pthread_mutex_unlock(&lock_mutex);

    // activate locks[new_lock_id], prepare file for future manipulation
    memset(&locks[new_lock_id].f_name, 0, sizeof(locks[new_lock_id].f_name));
    strcpy(locks[new_lock_id].f_name, filename);
    locks[new_lock_id].fd = fd;
    locks[new_lock_id].n_reader = 0;
    locks[new_lock_id].n_writer = 0;
    pthread_mutex_init(&locks[new_lock_id].f_mtx, NULL);
    pthread_cond_init(&locks[new_lock_id].f_cond, NULL);

    // success response
    echo->status = "OK";
    echo->code = fd;
    echo->message = "file opened successfully";

    return 0;
}

int seeker(int argc, char** argv, struct echo_t* echo, int lock_id) {
    // validate request format
    if (argc != 3) {
        echo->status = "FAIL";
        echo->code = -1;
        echo->message = "Usage: FSEEK identifier offset";
        return -1;
    }
    if (checkDigit(argv[1]) == 0 || checkDigit(argv[2]) == 0) {
        echo->status = "FAIL";
        echo->code = -5;
        echo->message = "invalid argument(s)";
        return -1;
    }

    int identifier = atoi(argv[1]);
    off_t offset = atoi(argv[2]);  // offset can be negative!
    struct lock_t* lock = &locks[lock_id];

    if (identifier != lock->fd || lock->fd <= 0) {
        echo->status = "ERR";
        echo->code = ENOENT;
        echo->message = "invalid identifier, no such file or directory";
        return -1;
    }

    // waiting for resources
    pthread_mutex_lock(&lock->f_mtx);
    while (lock->n_reader > 0 || lock->n_writer > 0) {
        pthread_cond_wait(&lock->f_cond, &lock->f_mtx);
    }
    lock->n_writer++;  // seek is equivalent to a write
    pthread_mutex_unlock(&lock->f_mtx);

    // seeking... all threads share one seek pointer on the same file
    int pos = lseek(identifier, offset, SEEK_CUR);  // position of the seek pointer
    if (pos == -1) {
        echo->status = "FAIL";
        echo->code = errno;
        echo->message = "system call lseek() returns -1";
        return -1;
    }

    // writing finished
    pthread_mutex_lock(&lock->f_mtx);
    lock->n_writer--;
    pthread_cond_broadcast(&lock->f_cond);
    pthread_mutex_unlock(&lock->f_mtx);

    // success response
    echo->status = "OK";
    echo->code = 0;
    char temp[100];
    memset(temp, 0, sizeof(temp));
    sprintf(temp, "seek pointer is now %d bytes from the beginning of the file", pos);
    echo->message = strdup(temp);

    return 0;
}

int reader(int argc, char** argv, struct echo_t* echo, int lock_id) {
    // validate request format
    if (argc != 3) {
        echo->status = "FAIL";
        echo->code = -1;
        echo->message = "Usage: FREAD identifier length";
        return -1;
    }
    if (checkDigit(argv[1]) == 0 || checkDigit(argv[2]) == 0) {
        echo->status = "FAIL";
        echo->code = -5;
        echo->message = "invalid argument(s)";
        return -1;
    }

    int identifier = atoi(argv[1]);
    int len = atoi(argv[2]);
    struct lock_t* lock = &locks[lock_id];

    if (len < 0) {
        echo->status = "FAIL";
        echo->code = -6;
        echo->message = "invalid length value";
        return -1;
    }

    if (identifier != lock->fd || lock->fd <= 0) {
        echo->status = "ERR";
        echo->code = ENOENT;
        echo->message = "invalid identifier, no such file or directory";
        return -1;
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
        char msg[128];
        memset(msg, 0, sizeof(msg));
        sprintf(msg, "client in thread %d starts reading...", (int)pthread_self());
        logger(msg);
        sleep(3);
    }
    char buf[4096];
    memset(buf, 0, sizeof(buf));
    int n = read(lock->fd, buf, len);
    if (n == -1) {
        echo->status = "FAIL";
        echo->code = errno;
        echo->message = "system call read() returns -1";
        return -1;
    }
    if (buf[strlen(buf) - 1] == '\n') {
        buf[strlen(buf) - 1] = '\0';
    }
    if (DELAY_MODE) {
        char msg[128];
        memset(msg, 0, sizeof(msg));
        sprintf(msg, "client in thread %d finished reading...", (int)pthread_self());
        logger(msg);
    }

    // reading finished
    pthread_mutex_lock(&lock->f_mtx);
    lock->n_reader--;
    pthread_cond_broadcast(&lock->f_cond);
    pthread_mutex_unlock(&lock->f_mtx);

    // success response
    echo->status = "OK";
    echo->code = n;
    echo->message = buf;

    return 0;
}

int writer(int argc, char** argv, struct echo_t* echo, int lock_id) {
    // validate request format
    if (argc != 3) {
        echo->status = "FAIL";
        echo->code = -1;
        echo->message = "Usage: FWRITE identifier bytes";
        return -1;
    }
    if (checkDigit(argv[1]) == 0) {
        echo->status = "FAIL";
        echo->code = -5;
        echo->message = "invalid argument(s)";
        return -1;
    }

    int identifier = atoi(argv[1]);
    char* buf = argv[2];

    struct lock_t* lock = &locks[lock_id];

    if (identifier != lock->fd || lock->fd <= 0) {
        echo->status = "ERR";
        echo->code = ENOENT;
        echo->message = "invalid identifier, no such file or directory";
        return -1;
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
        char msg[128];
        memset(msg, 0, sizeof(msg));
        sprintf(msg, "client in thread %d starts writing...", (int)pthread_self());
        logger(msg);
        sleep(6);
    }
    int len = strlen(buf);
    int total = 0;   // bytes sent
    int left = len;  // bytes left
    int n;
    while (total < left) {
        n = write(lock->fd, buf + total, left);  // update seek
        if (n == -1) {
            echo->status = "FAIL";
            echo->code = errno;
            echo->message = "system call write() returns -1";
            return -1;
        }
        total += n;
        left -= n;
    }
    if (DELAY_MODE) {
        char msg[128];
        memset(msg, 0, sizeof(msg));
        sprintf(msg, "client in thread %d finished writing...", (int)pthread_self());
        logger(msg);
    }

    // writing finished
    pthread_mutex_lock(&lock->f_mtx);
    lock->n_writer--;
    pthread_cond_broadcast(&lock->f_cond);
    pthread_mutex_unlock(&lock->f_mtx);

    // success response
    echo->status = "OK";
    echo->code = 0;
    echo->message = "data written to the file";

    return 0;
}

int closer(int argc, char** argv, struct echo_t* echo, int lock_id) {
    // validate request format
    if (argc != 2) {
        echo->status = "FAIL";
        echo->code = -1;
        echo->message = "Usage: FCLOSE identifier";
        return -1;
    }
    if (checkDigit(argv[1]) == 0) {
        echo->status = "FAIL";
        echo->code = -5;
        echo->message = "invalid argument(s)";
        return -1;
    }

    int identifier = atoi(argv[1]);
    struct lock_t* lock = &locks[lock_id];

    if (identifier != lock->fd || lock->fd <= 0) {
        echo->status = "ERR";
        echo->code = ENOENT;
        echo->message = "invalid identifier, no such file or directory";
        return -1;
    }

    // wait until no readers or writers
    pthread_mutex_lock(&lock->f_mtx);
    while (lock->n_reader > 0 || lock->n_writer > 0) {
        pthread_cond_wait(&lock->f_cond, &lock->f_mtx);
    }
    lock->n_writer++;
    pthread_mutex_unlock(&lock->f_mtx);

    // closing... first unlock the file
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(identifier, F_OFD_SETLK, &fl) == -1) {  // unlock file
        echo->status = "FAIL";
        echo->code = errno;
        echo->message = "cannot close (unlock) file";
        return -1;
    }

    // when we close this fd, all the locks on this physical file in the same process are released
    // even if the locks were made using other file descriptors that remain open (but we won't let this happen)
    if (close(identifier) < 0) {
        echo->status = "FAIL";
        echo->code = errno;
        echo->message = "cannot close file";
        return -1;
    }

    // upon close() success, reset the locks[lock_id] entry to avoid corrupt behavior in other threads
    locks[lock_id].fd = -1;
    reset_lock(lock_id);

    // success response
    echo->status = "OK";
    echo->code = 0;
    echo->message = "file closed";

    return 0;
}

void reset_lock(int lock_id) {
    if (locks[lock_id].fd > 0) {
        close(locks[lock_id].fd);
    }
    locks[lock_id].fd = -1;
    // unlink(locks[lock_id].f_name);  // should not delete file
    memset(&locks[lock_id].f_name, 0, sizeof(locks[lock_id].f_name));
    locks[lock_id].n_reader = 0;
    locks[lock_id].n_writer = 0;
    pthread_mutex_destroy(&locks[lock_id].f_mtx);  // release resource
    pthread_cond_destroy(&locks[lock_id].f_cond);  // release resource
}

void broadcast_sync(const char* req) {
    // notify other peers to sync (one-phase commit)
    for (int i = 0; i < num_peers - 1; i++) {
        if (send(csocks[i], req, strlen(req), 0) < 0) {
            char info[128];
            memset(info, 0, sizeof(info));
            sprintf(info, "failed to send sync request, peer node %s unavailable", peers[i + 1]);
            logger(info);
            continue;
        }
    }
}

void clean_client(int csock) {
    char msg[128];
    memset(msg, 0, sizeof(msg));
    sprintf(msg, "closing client connection on socket %d", csock);
    logger(msg);

    shutdown(csock, SHUT_WR);  // civilized server shutdown first before close
    close(csock);
}

void serve_client(int csock) {
    const char* welcome = "Welcome to the database! \nAvailable commands: FOPEN FSEEK FREAD FWRITE FCLOSE QUIT\n";
    const char* prompt = "> ";

    // welcome client socket and add it to poll
    int n_res;
    struct pollfd cfds[1];
    cfds[0].fd = csock;
    cfds[0].events = POLLIN;
    send(csock, welcome, strlen(welcome), 0);

    // repeatedly receive a request from client and handle it
    struct echo_t echo;
    int lock_id = 0;  // specify an entry in struct lock_t locks[]
    char* filename = NULL;
    while (1) {
        if (send(csock, prompt, strlen(prompt), 0) < 0) {
            if (errno == EPIPE) {
                char msg[128];
                memset(msg, 0, sizeof(msg));
                sprintf(msg, "connection closed by client on socket %d", csock);
                logger(msg);
                break;
            }
            perror("send");
            fflush(stderr);
            break;
        }

        if ((n_res = poll(cfds, 1, 60000)) != 0) {  // time out after 1 minute of inactivity
            if (n_res < 0) {
                perror("poll");
                fflush(stderr);
                break;
            }

            char req[256];
            memset(req, 0, sizeof(req));
            int n_bytes = 0;  // number of bytes received

            // receive a client request
            if (cfds[0].revents & POLLIN) {
                n_bytes = recv(csock, req, sizeof(req) - 1, 0);
                if (n_bytes == 0) {
                    char msg[128];
                    memset(msg, 0, sizeof(msg));
                    sprintf(msg, "connection closed by client on socket %d", csock);
                    logger(msg);
                    break;
                }
                else if (n_bytes < 0) {
                    perror("recv");
                    fflush(stderr);
                    break;
                }
            }

            // save the raw request before it's mutated
            char xreq[strlen(req) + 128];
            memset(xreq, 0, sizeof(xreq));
            strncpy(xreq, req, strlen(req));

            // replace the newline
            int slen = strlen(req);
            if (slen > 0 && req[slen - 1] == '\n') {
                req[slen - 1] = '\0';
                if (req[slen - 2] == '\r') req[slen - 2] = '\0';  // windows CRLF \r\n
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
            char request[256];
            memset(request, 0, sizeof(request));
            if (strcasecmp(argv[0], "FOPEN") == 0) {
                if ((opener(argc, argv, &echo, &lock_id, &filename)) == 0) {  // open the file and assign a lock_id
                    broadcast_sync(xreq);
                }
            }
            else if (strcasecmp(argv[0], "FSEEK") == 0) {
                if ((seeker(argc, argv, &echo, lock_id)) == 0) {
                    sprintf(request, "%s %s %s", argv[0], filename, argv[2]);
                    broadcast_sync(request);
                }
            }
            else if (strcasecmp(argv[0], "FREAD") == 0) {
                if ((reader(argc, argv, &echo, lock_id)) == 0) {
                    // fflush socket rsock in case there are pending garbage data
                    char garbage[256];
                    for (int i = 0; i < num_peers - 1; i++) {
                        recvTimeOut(csocks[i], garbage, sizeof(garbage), 50);  // 50 ms
                    }
                    // broadcast fread requests
                    sprintf(request, "%s %s %s", argv[0], filename, argv[2]);
                    broadcast_sync(request);
                    // await all peer responses to vote
                    char response[num_peers][256];
                    sprintf(response[0], "%s", echo.message);
                    for (int i = 0; i < num_peers - 1; i++) {
                        memset(response[i + 1], 0, sizeof(response[i + 1]));
                        int x_bytes = recvTimeOut(csocks[i], response[i + 1], sizeof(response[i + 1]), 500);  // 500 ms
                        if (x_bytes <= 0) {
                            memset(response[i + 1], 0, sizeof(response[i + 1]));
                            sprintf(response[i + 1], "SYNC FAIL");
                        }
                    }
                    // ...................
                    int maxCount = 0;
                    int vote = -1;
                    for (int i = 0; i < num_peers; i++) {
                        int count = 0;
                        for (int j = 0; j < num_peers; j++) {
                            if (strcmp(response[i], response[j]) == 0) count++;
                        }

                        if(count > maxCount) {
                            maxCount = count;
                            vote = i;
                        }
                    }

                    echo.message = strdup(response[vote]);
                    echo.code = strlen(echo.message);
                    if (strcmp(echo.message, "SYNC FAIL") == 0) {
                        echo.status = "FAIL";
                        echo.code = 99;
                    }
                }
            }
            else if (strcasecmp(argv[0], "FWRITE") == 0) {
                if ((writer(argc, argv, &echo, lock_id)) == 0) {
                    sprintf(request, "%s %s %s", argv[0], filename, argv[2]);
                    broadcast_sync(request);
                }
            }
            else if (strcasecmp(argv[0], "FCLOSE") == 0) {
                if ((closer(argc, argv, &echo, lock_id)) == 0) {
                    sprintf(request, "%s %s", argv[0], filename);
                    broadcast_sync(request);
                }
            }
            else if (strcasecmp(argv[0], "QUIT") == 0) {
                break;  // bye
            }
            else {  // invalid command
                echo.status = "FAIL";
                echo.code = -9;
                echo.message = "invalid request";
            }

            // send response to client
            char res[4096];
            memset(res, 0, sizeof(res));

            sprintf(res, "%s", echo.status);
            sprintf(res + strlen(res), " %d", echo.code);
            sprintf(res + strlen(res), " %s", echo.message);
            int len = strlen(res);
            res[len] = '\n';
            len++;

            if (sendAll(csock, res, &len) == -1) {
                perror("sendall");
                printf("only %d bytes of data have been sent!\n", len);
                fflush(stdout); fflush(stderr);
                break;
            }
        }
        else {  // will reach here only if poll() timed out
            const char* farewell = "your session has expired\n";
            int len = strlen(farewell);
            if (sendAll(csock, farewell, &len) == -1) {  // say good-bye to client
                perror("sendall2");
                printf("only %d bytes of data have been sent!\n", len);
                fflush(stdout); fflush(stderr);
                break;
            }
            break;  // bye
        }
    }
}

void* file_thread(void* id) {
    struct sockaddr_storage cli_addr;
    socklen_t sin_size = sizeof(cli_addr);
    char ipstr[INET6_ADDRSTRLEN];

    // add master socket to poll
    struct pollfd pfds[1];
    pfds[0].fd = fsock;
    pfds[0].events = POLLIN;

    while (1) {
        // accept incoming clients or block if there's no client
        pthread_mutex_lock(&wake_mutex);  // a wake mutex enforces no concurrent calls to accept, threads must wake up one by one
        int csock = accept(pfds[0].fd, (struct sockaddr*)&cli_addr, &sin_size);
        pthread_mutex_unlock(&wake_mutex);

        if (csock == -1) {
            if (fsock == -1 && errno == EBADF) {  // master socket temporarily closed by dynamic reconfiguration
                pthread_mutex_lock(&monitor.m_mtx);
                monitor.t_tot--;
                pthread_mutex_unlock(&monitor.m_mtx);
                pthread_exit(NULL);  // thread quits normally
            }
            perror("accept");  // system call failed
            fflush(stderr);
            pthread_exit((void*)-13);  // thread quits with error
        }

        // new client connected
        inet_ntop(cli_addr.ss_family, extractAddr((struct sockaddr*)&cli_addr), ipstr, INET6_ADDRSTRLEN);
        char msg[128];
        memset(msg, 0, sizeof(msg));
        sprintf(msg, "new connection from %s on socket %d", ipstr, csock);
        logger(msg);

        // thread is now busy serving the client
        pthread_mutex_lock(&monitor.m_mtx);
        monitor.t_act++;
        pthread_cond_signal(&monitor.m_cond);  // when we increment, notify the monitor to check threads occupancy
        pthread_mutex_unlock(&monitor.m_mtx);

        thread_pool[(int)(intptr_t)id].idle = 0;
        serve_client(csock);

        // thread now becomes idle
        pthread_mutex_lock(&monitor.m_mtx);
        monitor.t_act--;
        pthread_mutex_unlock(&monitor.m_mtx);

        thread_pool[(int)(intptr_t)id].idle = 1;
        clean_client(csock);

        // thread quits itself if there's too little network traffic
        int lower_bound = monitor.t_tot - monitor.t_inc;
        if (monitor.t_act < lower_bound) {
            pthread_mutex_lock(&monitor.m_mtx);
            monitor.t_tot--;
            pthread_mutex_unlock(&monitor.m_mtx);
            pthread_exit(NULL);
        }
    }
}
