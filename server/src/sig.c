/*
** sig.c -- a separate thread for receiving and handling signals.
*/

#include "define.h"

void zombieReaper(int signal) {
    (void) signal;  // suppress the warning of unused variable
    int saved_errno = errno;  // waitpid() might overwrite errno, so we save and restore it
    while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
    errno = saved_errno;
}

void* signal_thread(void* set) {
    sigset_t* signals = (sigset_t*)set;
    int sig;
    while (1) {
        if (sigwait(signals, &sig) != 0) {  // block until signals arrive
            perror("sigwait");
            fflush(stderr);
            exit(183);
        }

        switch (sig) {
            case SIGCHLD:
                break;  // just ignore SIGCHLD since we explicitly wait on child
            case SIGQUIT:
                printf("received signal \"%s\" (%d), stopping server...\n", strsignal(sig), sig);
                fflush(stdout);
                stop_server();
                break;
            case SIGHUP:
                printf("received signal \"%s\" (%d), reloading server...\n", strsignal(sig), sig);
                fflush(stdout);
                reset_server();
                break;
            default:
                printf("received signal \"%s\" (%d)\n", strsignal(sig), sig);
                fflush(stdout);
                break;
        }
    }
}
