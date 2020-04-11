/*
** sserv.c -- shell service (internal use only)
*/

#include "define.h"

void server_admin(int asock) {
    const char* welcome = "Welcome to the daemon! Please issue your shell command, or type QUIT to exit.\n"
                          "You can type MONITOR to view the current threads usage, hit Enter to stop.\n";
    const char* prompt = "> ";
    const char* path[] = {"/bin", "/usr/bin", 0};

    // prepare for execution
    int status = 0;     // exit status of the child process
    int executed = 0;   // check if a shell command has been issued
    char output[4096];  // buffer to store the shell command output

    int channel[2];  // create a pipe for IPC
    if (pipe(channel) == -1) {
        perror("pipe");
        fflush(stderr);
        exit(16);
    }

    // welcome admin socket and add it to poll
    int n_res;
    struct pollfd pfds[1];
    pfds[0].fd = asock;
    pfds[0].events = POLLIN;
    send(asock, welcome, strlen(welcome), 0);

    // repeatedly receive an admin command and handle it
    while (1) {
        if (send(asock, prompt, strlen(prompt), 0) < 0) {
            if (errno == EPIPE) {
                char msg[128];
                memset(msg, 0, sizeof(msg));
                sprintf(msg, "connection closed by admin on socket %d\n", asock);
                logger(msg);
                break;
            }
            perror("send");
            fflush(stderr);
            break;
        }

        retry:
        if ((n_res = poll(pfds, 1, 300000)) != 0) {  // time out after 5 minutes of inactivity
            if (errno == EINTR) goto retry;
            if (n_res < 0) {
                perror("poll");
                fflush(stderr);
                break;
            }

            char req[256];
            memset(req, 0, sizeof(req));
            int n_bytes;  // number of bytes received

            // receive an admin request
            if (pfds[0].revents & POLLIN) {
                n_bytes = recv(asock, req, sizeof(req) - 1, 0);
                if (n_bytes == 0) {
                    char msg[128];
                    memset(msg, 0, sizeof(msg));
                    sprintf(msg, "connection closed by admin on socket %d\n", asock);
                    logger(msg);
                    break;
                }
                else if (n_bytes < 0) {
                    perror("recv");
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

            // if admin wants to quit
            if (strcasecmp(req, "QUIT") == 0) {
                logger("admin is now offline");
                break;  // bye
            }

            // parse admin request to obtain argv[]
            char* envp[] = { NULL };
            char* tokens[strlen(req)];
            char** argv = tokens;
            int argc = tokenize(req, argv, strlen(req));
            argv[argc] = 0;

            // if admin just pressed Enter('\n'), start over
            if (strlen(argv[0]) == 0) {
                continue;
            }

            // ready to execute command
            struct echo_t echo;
            if (strcasecmp(argv[0], "CPRINT") == 0) {
                // send shell output on request
                if (executed == 0) {
                    echo.status = "ERR";
                    echo.code = EIO;
                    echo.message = "No command has been issued";
                }
                else {
                    if (send(asock, output, strlen(output), 0) < 0) {
                        perror("send");
                        fflush(stderr);
                        echo.status = "FAIL";
                        echo.code = -7;
                        echo.message = "Failed to send output";
                    }
                    else {
                        echo.status = "OK";
                        echo.code = 0;
                        echo.message = "Last executed shell output sent";
                    }
                }
            }
            else if (strcasecmp(argv[0], "MONITOR") == 0) {
                // display threads usage info per second until admin hits Enter
                char info[128];
                while (1) {
                    memset(info, 0, sizeof(info));
                    sprintf(info, "Threads Usage: %d out of %d total threads are currently active\n", monitor.t_act, monitor.t_tot);
                    send(asock, info, strlen(info), 0);
                    memset(info, 0, sizeof(info));
                    int x_bytes = recvTimeOut(asock, info, sizeof(info), 1000);  // non-block recv()
                    if (x_bytes == -1) {
                        if (errno == EINTR) { continue; }
                        perror("recvTimeOut");
                        fflush(stderr);
                        exit(47);
                    }
                    else if (x_bytes == 0 || x_bytes == -2) {  // no data
                        continue;
                    }
                    if (info[0] == '\n' || info[0] == '\r')  break;
                }
                continue;
            }
            else {
                pid_t child = fork();

                // child exec() the shell command
                if (child == 0) {
                    close(1);  // stdout
                    close(2);  // stderr
                    dup(channel[1]);  // redirect child's STDOUT to channel[1]
                    dup(channel[1]);  // redirect child's STDERR to channel[1]
                    close(channel[0]);
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
                        int n_bytes = readTimeOut(channel[0], output, sizeof(output), 200);  // non-block read()
                        if (n_bytes == -1) {
                            if (errno == EINTR) { continue; }
                            perror("readTimeOut");
                            fflush(stderr);
                            exit(713);
                        }
                        else if (n_bytes == 0 || n_bytes == -2) {  // EOF, no data in the pipe
                            break;
                        }
                    }
                    waitpid(child, &status, 0);
                }

                // only parent continues
                if (strlen(output) == 0) {
                    echo.status = "FAIL";
                    echo.code = -4;
                    echo.message = "Failed to execute the command";
                }
                else {
                    executed = 1;  // command executed, but may still have an error condition
                    echo.code = status;
                    if (status == 0) {
                        echo.status = "OK";
                        echo.message = "Command executed successfully";
                    }
                    else {
                        echo.status = "ERR";
                        echo.message = "Command executed with errors";
                    }
                }
            }

            // send response to admin
            char res[4096];
            memset(res, 0, sizeof(res));

            sprintf(res, "%s", echo.status);
            sprintf(res + strlen(res), " %d", echo.code);
            sprintf(res + strlen(res), " %s", echo.message);
            int len = strlen(res);
            res[len] = '\n';
            len++;

            if (sendAll(asock, res, &len) == -1) {
                perror("sendall3");
                printf("only %d bytes of data have been sent!\n", len);
                fflush(stdout); fflush(stderr);
                break;
            }
        }

        else {  // will reach here only if poll() timed out
            const char* farewell = "your session has expired\n";
            int len = strlen(farewell);
            if (sendAll(asock, farewell, &len) == -1) {  // say good-bye to client
                perror("sendall4");
                printf("only %d bytes of data have been sent!\n", len);
                fflush(stdout); fflush(stderr);
                break;
            }

            char msg[128];
            memset(msg, 0, sizeof(msg));
            sprintf(msg, "closing admin connection on socket %d", asock);
            logger(msg);
            break;
        }
    }

    // end this session
    shutdown(asock, SHUT_WR);  // civilized server shutdown first before close
    close(asock);
    close(channel[0]);  // close pipe
    close(channel[1]);  // close pipe
}
