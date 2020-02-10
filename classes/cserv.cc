/*
 * Simple concurrent client which repeatedly receives lines from
 * clients and return them prefixed by the string "ACK: ".  Release
 * the connection with a client upon receipt of the string "quit"
 * (case sensitive) or upon a connection close by the client.
 *
 * Of course, the concurrent variant is almost identical to the purely
 * iterative one (i.e., the one that serves one client at a time),
 * except for the call to fork.  Do compare the complexity of this
 * code with the complexity of the code that simulates concurrency.
 *
 * By Stefan Bruda
 */

#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include "tcp-utils.h"

/*
 * Repeatedly receives requests from the client and responds to them.
 * If the received request is an end of file or "quit", terminates the
 * connection.  Note that an empty request also terminates the
 * connection.  Same as for the purely iterative server.
 */
void do_client (int sd) {
    const int ALEN = 256;
    char req[ALEN];
    const char* ack = "ACK: ";
    int n;
    
    // Loop while the client has something to say...
    while ((n = readline(sd,req,ALEN-1)) != recv_nodata) {
        if (strcmp(req,"quit") == 0) {
            printf("Received quit, sending EOF.\n");
            shutdown(sd,1);
            return;
        }
        send(sd,ack,strlen(ack),0);
        send(sd,req,strlen(req),0);
        send(sd,"\n",1,0);
    }
    // read 0 bytes = EOF:
    printf("Connection closed by client.\n");
    shutdown(sd,1);
}

/*
 * Cleans up "zombie children," i.e., processes that did not really
 * die.  Will run each time the parent receives a SIGCHLD signal.
 */
void cleanup_zombies (int sig) {
    int status;
    printf("Cleaning up... \n");
    while (waitpid(-1,&status,WNOHANG) >= 0)
        /* NOP */;
}

int main (int argc, char** argv) {
    const int port = 9001;   // port to listen to
    const int qlen = 32;     // incoming connections queue length

    int msock, ssock;               // master and slave socket
    struct sockaddr_in client_addr; // the address of the client...
    unsigned int client_addr_len = sizeof(client_addr); // ... and its length
    
    msock = passivesocket(port,qlen);
    if (msock < 0) {
        perror("passivesocket");
        return 1;
    }
    printf("Server up and listening on port %d.\n", port);
    
    signal(SIGCHLD,cleanup_zombies);  // clean up errant children...
    
    while (1) {
        // Accept connection:
        ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (ssock < 0) {
            perror("accept");
            return 1;
        }
        
        int pid = fork();
        if (pid < 0)
            perror("fork");
        else if (pid == 0) {  // child, manages the client-server communication
            close(msock);     // child does not listen to msock...
            printf("Incoming client.\n");
            do_client(ssock);
            close(ssock);
            printf("Outgoing client removed.\n");
            return 0;
        }
        else // parent
            close(ssock);  // parent has no use for ssock...
    }
    return 0;   // will never reach this anyway...
}
