/*
** sync.c -- protocol for peer (replica server nodes) synchronization
*/

#include "define.h"

void* sync_thread(void* id) {
    int index = (int)(intptr_t)id;
    int n_res;
    struct pollfd sfds[1];
    sfds[0].fd = ssocks[index];
    sfds[0].events = POLLIN;

    // await sync requests from the peer socket
    struct echo_t echo;
    int lock_id = 0;  // specify an entry in struct lock_t locks[]
    char* filename = NULL;

    while (1) {
        if ((n_res = poll(sfds, 1, -1)) != 0) {
            if (n_res < 0) {
                logger("exception: failed poll in sync thread");
                break;
            }

            char req[256];
            memset(req, 0, sizeof(req));
            int n_bytes = 0;

            if (sfds[0].revents & POLLIN) {
                n_bytes = recv(sfds[0].fd, req, sizeof(req) - 1, 0);
                if (n_bytes == 0) {
                    char msg[128];
                    memset(msg, 0, sizeof(msg));
                    sprintf(msg, "connection closed by peer on socket %d", sfds[0].fd);
                    logger(msg);
                    break;
                } else if (n_bytes < 0) {
                    perror("peer recv");
                    fflush(stderr);
                    break;
                }
            }

            // replace the newline
            int slen = strlen(req);
            if (slen > 0 && req[slen - 1] == '\n') {
                req[slen - 1] = '\0';
                if (req[slen - 2] == '\r') req[slen - 2] = '\0';  // windows CRLF \r\n
            }

            // parse peer request to obtain argv[]
            char* tokens[strlen(req)];
            char** argv = tokens;
            int argc = tokenize(req, argv, strlen(req));
            argv[argc] = 0;

            // find local lock_id (the same filename may have a different lock_id in another node)
            for (int i = 0; i < n_lock; i++) {
                if (strcmp(locks[i].f_name, argv[1]) == 0) {
                    lock_id = i;
                    break;
                }
            }

            // execute peer request
            if (strcasecmp(argv[0], "FOPEN") == 0) {
                int dummy_lock_id = 0;
                opener(argc, argv, &echo, &dummy_lock_id, &filename);
            }
            else if (strcasecmp(argv[0], "FSEEK") == 0) {
                sprintf(argv[1], "%d", locks[lock_id].fd);
                seeker(argc, argv, &echo, lock_id);
            }
            else if (strcasecmp(argv[0], "FREAD") == 0) {
                sprintf(argv[1], "%d", locks[lock_id].fd);
                struct timeval stop;
                struct timeval start;
                gettimeofday(&start, NULL);
                if (reader(argc, argv, &echo, lock_id) == 0) {
                    // send response to the peer node only if it's a fast successful read
                    gettimeofday(&stop, NULL);
                    long sec = stop.tv_sec - start.tv_sec;
                    long msec = (stop.tv_usec - start.tv_usec)/1000;
                    if (sec == 0 && msec < 500) {
                        int len = strlen(echo.message);
                        if (sendAll(sfds[0].fd, echo.message, &len) == -1) {
                            perror("sendall");
                            printf("only %d bytes of data have been sent!\n", len);
                            fflush(stdout); fflush(stderr);
                            break;
                        }
                    }
                }
            }
            else if (strcasecmp(argv[0], "FWRITE") == 0) {
                sprintf(argv[1], "%d", locks[lock_id].fd);
                writer(argc, argv, &echo, lock_id);
            }
            else if (strcasecmp(argv[0], "FCLOSE") == 0) {
                sprintf(argv[1], "%d", locks[lock_id].fd);
                closer(argc, argv, &echo, lock_id);
            }
            else {  // invalid command
                continue;
            }
        }
    }
    pthread_exit((void*)-91);  // exit thread on exception
}

void* server_thread(void* omitted) {
    struct sockaddr_storage cli_addr;
    socklen_t sin_size = sizeof(cli_addr);
    struct pollfd pfds[1];
    pfds[0].fd = psock;
    pfds[0].events = POLLIN;

    for (int i = 0; i < num_peers - 1; i++) {
        ssocks[i] = accept(pfds[0].fd, (struct sockaddr*)&cli_addr, &sin_size);
        if (ssocks[i] == -1) {
            perror("accept");
            fflush(stderr);
            exit(133);
        }
        pthread_t scid;
        if (pthread_create(&scid, &attr, sync_thread, (void*)(intptr_t)i) != 0) {
            perror("pthread_create");
            fflush(stderr);
            exit(21);
        }
    }
    pthread_exit(NULL);
}

void* client_thread(void* omitted) {
    char* host;
    char* port;
    for (int i = 0; i < num_peers - 1; i++) {
        // split host:port pair
        char* peer = strdup(peers[i + 1]);
        char* tokens[strlen(peer)];
        char** vec = tokens;
        int n_tok = str_split(peer, vec, strlen(peer));
        vec[n_tok] = 0;
        host = vec[0];
        port = vec[1];

        while (csocks[i] <= 0) {
            // attempt to connect to this peer node
            csocks[i] = socketConnect(host, port);
            // on success, create a new thread for this peer
            if (csocks[i] > 0) {
                char dinfo[128]; memset(dinfo, 0, sizeof(dinfo));
                sprintf(dinfo, "connected to peer node %s (port %s)", host, port);
                logger(dinfo);
                break;
            }
            // on failure, retry after 1 second
            sleep(1);
        }
    }
    pthread_exit(NULL);  // exit thread after connected to all peers
}
