/*
** client.c -- a stream socket client demo
**
** @example: > ./client localhost
**           client: connected to 127.0.0.1
**           client: received Hello, client!
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT "3490"
#define MAXDATASIZE 4096  // max number of bytes to recv() in one shot

void* getInAddr(struct sockaddr* sa) {
	return (sa->sa_family == AF_INET)
	? &(((struct sockaddr_in*)sa)->sin_addr)
	: &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char* argv[]) {
	int status, sockfd, n_bytes;
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	char ipstr[INET6_ADDRSTRLEN];

	if (argc != 2) {
	    fprintf(stderr,"usage: client hostname\n");
	    exit(1);
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((status = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		return 1;
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			close(sockfd);
			continue;
		}

		break;  // connect to the first reachable address we can find
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, getInAddr((struct sockaddr*)p->ai_addr), ipstr, sizeof(ipstr));
	printf("client: connected to %s\n", ipstr);

	freeaddrinfo(servinfo);

	if ((n_bytes = recv(sockfd, buf, MAXDATASIZE - 1, 0)) <= 0) {
	    if (n_bytes == 0) {
	        printf("socket connection %d closed\n", sockfd);  // connection closed
	    } else {
	        perror("recv");
			exit(1);
	    }
	}

	buf[n_bytes] = '\0';
	printf("client: received %s\n", buf);

	close(sockfd);

	return 0;
}
