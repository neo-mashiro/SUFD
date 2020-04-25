/*
** sshell.c -- a simple shell that talks to the remote server
**
** @author: Wentao Lu
** @date: 2020/02/15
*/

#include <stdio.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "utils.h"

// global settings
const char* path[] = {"/bin", "/usr/bin", 0};
const char* prompt = "sshell > ";
const char* config = "config.ini";

// configuration
size_t hsize = 0, vsize = 0;  // terminal dimensions
char host[256];               // host name
unsigned int port = 0;        // port number

// define handler type
typedef void (*handler_t)(int);

/* define zombie reaper handler (enable) */
void enable(int signal) {
	(void)signal;  // suppress the warning of unused variable
	int saved_errno = errno;  // waitpid() might overwrite errno, so we save and restore it
	while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
	errno = saved_errno;
}

/* define zombie reaper handler (disable) */
void disable(int signal) {}

/* register zombie reaper handler */
void registerZombieReaper(struct sigaction* sa_ptr, handler_t f) {
    sa_ptr->sa_handler = f;  // user-defined handler
    if (sigaction(SIGCHLD, sa_ptr, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
}

int execCommand(const char* command, char* const argv[], char* const envp[], const char** path, struct sigaction* sa_ptr) {
    /*
    ** execute an external command with argv[] within envp[]
    **
    ** @param:  a command, argv[], envp[], a null-terminated path, a sigaction pointer
    ** @return: a status integer returned by waitpid(), or errno
    ** @remark: this function awaits the completion of the command (synchronous)
    **          it will attempt to prefix the command with path if execution failed
    */

    // when we wait child processes (synchronous), they won't become zombies upon termination
    registerZombieReaper(sa_ptr, disable);

    int childp = fork();
    int status = 0;

    if (childp == 0) {  // child
        execve(command, argv, envp);  // attempt to execute with no path prefix ...
        for (size_t i = 0; path[i] != 0; i++) {  // then try with path prefixed
            char* cp = new char[strlen(path[i]) + strlen(command) + 2];
            sprintf(cp, "%s/%s", path[i], command);
            execve(cp, argv, envp);
            delete[] cp;
        }

        // if execve() failed and errno set
        char* message = new char[strlen(command) + 10];
        sprintf(message, "exec %s", command);
        perror(message);
        delete[] message;
        exit(errno);  // exit so that the function does not return twice!
    }
    else {  // parent
        waitpid(childp, &status, 0);
        registerZombieReaper(sa_ptr, enable);  // restore the zombie reaper handler after wait
        return status;
    }
}

int main(int argc, char** argv, char** envp) {
    struct sigaction sa;  // signal action
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    char command[256];   // buffer for commands
    command[255] = '\0';
    char* com_tok[256];  // buffer for the tokenized commands
    size_t num_tok;      // number of tokens

    int sock = -1;       // socket file descriptor, -1 = nonexist
    int keepalive = 0;   // keepalive mode is disabled by default

    // print welcome message on startup
    printf("Welcome to simple shell.\n");

    // open or create configuration file
    int confd = open(config, O_RDONLY);
    if (confd < 0) {
        perror("config");
        fprintf(stderr, "config: cannot open the configuration file.\n");
    }

    // load configuration file
    while (hsize == 0 || vsize == 0 || host[0] == 0 || port == 0) {
        int n = readLine(confd, command, 255);
        if (n == -2) {  // no data
            break;
        }
        if (n < 0) {
            sprintf(command, "config: %s", config);
            perror(command);
            break;
        }
        num_tok = tokenize(command, com_tok, strlen(command));

        if (strcmp(com_tok[0], "VSIZE") == 0 && atol(com_tok[1]) > 0) {
            vsize = atol(com_tok[1]);
        }
        else if (strcmp(com_tok[0], "HSIZE") == 0 && atol(com_tok[1]) > 0) {
            hsize = atol(com_tok[1]);
        }
        else if (strcmp(com_tok[0], "RHOST") == 0) {
            strcpy(host, com_tok[1]);
        }
        else if (strcmp(com_tok[0], "RPORT") == 0 && atoi(com_tok[1]) > 0) {
            port = atoi(com_tok[1]);
        }
    }
    close(confd);

    // validate configuration
    if (hsize <= 0) {  // invalid horizontal size, will use defaults and write to configuration
        hsize = 75;
        confd = open(config, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        write(confd, "HSIZE 75\n", strlen("HSIZE 75\n"));
        close(confd);
        fprintf(stderr, "%s: invalid horizontal terminal size, will use the default\n", config);
    }
    if (vsize <= 0) {  // invalid vertical size, will use defaults and write to configuration
        vsize = 40;
        confd = open(config, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        write(confd, "VSIZE 40\n", strlen("VSIZE 40\n"));
        close(confd);
        fprintf(stderr, "%s: invalid vertical terminal size, will use the default\n", config);
    }
    if (port > 65535) {  // invalid port number
        port = 9001;
        confd = open(config, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        write(confd, "RPORT 9001\n", strlen("RPORT 9001\n"));
        close(confd);
        fprintf(stderr, "%s: invalid port number, will use the default 9001\n", config);
    }

    printf("Terminal set to %ux%u.\n", (unsigned int)hsize, (unsigned int)vsize);

    // register the zombie reaper handler
    registerZombieReaper(&sa, enable);

    // main loop
    while (1) {
        printf("%s", prompt);
        fflush(stdin);

        // read command line input
        if (fgets(command, 255, stdin) == 0) {  // will include the newline in the buffer
            printf("\nBye\n");  // EOF, will quit
            return 0;
        }
        if (strlen(command) > 0 && command[strlen(command) - 1] == '\n') {
            command[strlen(command) - 1] = '\0';  // replace the newline
        }

        // tokenize command line input
        char** real_com = com_tok;
        num_tok = tokenize(command, real_com, strlen(command));
        real_com[num_tok] = 0;  // null termination for execve()

        // set command mode flags (local/remote, foreground/background)
        int rm = 1;     // command is remote by default
        int bg = 0;     // command is foreground by default

        // both `! & command` and `& ! command` are possible
        if (strcmp(real_com[0], "!") == 0) {
            rm = 0;  // local command
            real_com++;
            num_tok--;
            if (strcmp(real_com[0], "&") == 0) {
                bg = 1;  // background command
                real_com++;
                num_tok--;
            }
        }
        else if (strcmp(real_com[0], "&") == 0) {
            bg = 1;  // background command
            real_com++;
            num_tok--;
            if (strcmp(real_com[0], "!") == 0) {
                rm = 0;  // local command
                real_com++;
                num_tok--;
            }
        }

        // command string void of prefixes but suffixed with '\n', used as request to send() to server
        char req[256];
        memset(req, 0, sizeof(req));
        for (unsigned int i = 0; i < num_tok; i++) {
            if (i == 0) {
                sprintf(req, "%s", real_com[i]);
            }
            else {
                sprintf(req + strlen(req), " %s", real_com[i]);
            }
        }
        req[strlen(req)] = '\n';

        // check null input
        if (strlen(real_com[0]) == 0) {  // no command, user pressed Enter
            continue;
        }

        // local command
        if (rm == 0) {

            // internal command
            if (strcmp(real_com[0], "exit") == 0) {
                printf("Bye\n");
                return 0;
            }
            else if (strcmp(real_com[0], "keepalive") == 0) {
                keepalive = 1;
                printf("keepalive mode turned on.\n");
            }
            else if (strcmp(real_com[0], "close") == 0) {
                printf("client: connection closed\n");
                socketClose(&sock);
                keepalive = 0;
                printf("keepalive mode turned off.\n");
            }

            // external command
            else {
                if (bg) {  // background command, fork a process that awaits its completion
                    int monitor = fork();
                    if (monitor == 0) {  // monitor
                        int status = execCommand(real_com[0], real_com, envp, path, &sa);
                        printf("& %s done (%d)\n", real_com[0], WEXITSTATUS(status));
                        if (status != 0) {
                            printf("& %s completed with a non-null exit code\n", real_com[0]);
                        }
                        return 0;
                    }
                    else {  // parent continues and accepts the next command
                        continue;
                    }
                }
                else {  // foreground command, execute it and wait for completion
                    int status = execCommand(real_com[0], real_com, envp, path, &sa);
                    if (status != 0) {
                        printf("%s completed with a non-null exit code (%d)\n", real_com[0], WEXITSTATUS(status));
                    }
                }
            }
        }

        // remote command
        else {
            int rc;

            // fire up socket connection
            if (keepalive == 0 || sock == -1) {
                sock = socketConnect(host, convertPort(port));
                if (sock < 0) {
                    socketClose(&sock);
                    exit(sock);
                }
            }

            // background command
            if (bg == 1) {
                int childp = fork();
                if (childp == 0) {  // child, socket is copied, reference count + 1
                    if ((rc = socketTalk(sock, req, 6500, host)) < 0) {
                        socketClose(&sock);  // child silently close connection (no message) unless error occured
                        printf("client child: socketTalk() returns %d, connection closed\n", rc);
                        exit(rc);
                    }
                    printf("& %s done (%d)\n", real_com[0], WEXITSTATUS(rc));
                    socketClose(&sock);  // silently close connection, reference count - 1
                    return 0;  // child must terminate and not continue its loop
                }
                else {  // parent
                    printf("client: remote command running in background\n");
                    if (keepalive == 0) {
                        printf("client: connection closed\n");
                        socketClose(&sock);  // close socket if keepalive is off, otherwise keep connection
                    }
                    continue;
                }
            }
            // foreground command
            else {
                if ((rc = socketTalk(sock, req, 6500, host)) < 0) {
                    socketClose(&sock);
                    printf("client: socketTalk() returns %d, connection closed\n", rc);
                    exit(rc);
                }
                if (keepalive == 0) {
                    printf("client: connection closed\n");
                    socketClose(&sock);  // close socket if keepalive is off, otherwise keep connection
                }
            }
        }
    }
}
