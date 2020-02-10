/*
 * Simple iterative client which repeatedly receives lines from
 * clients and return them prefixed by the string "ACK: ".  Release
 * the connection with a client upon receipt of the string "quit"
 * (case sensitive) or upon a connection close by the client.
 *
 * By Stefan Bruda
 */

#include <stdio.h>
#include "tcp-utils.h"

/*
 * Repeatedly receives requests from the client and responds to them.
 * If the received request is an end of file or "quit", terminates the
 * connection.  Note that an empty request also terminates the
 * connection.
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

int main (int argc, char** argv) {
    const int port = 9000;   // port to listen to
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
    
    while (1) {
        // Accept connection:
        ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (ssock < 0) {
            perror("accept");
            return 1;
        }
        
        printf("Incoming client.\n");
        // Client-server communication
        do_client(ssock);  
        printf("Outgoing client removed.\n");
        close(ssock);
    }
    return 0;   // will never reach this anyway...
}
