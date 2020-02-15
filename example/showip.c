/*
** showip.c -- show IP addresses for a host given on the command line
**
** @example: > ./showip www.google.com
**           IP addresses for www.google.com:
**             IPv4: 172.217.164.228
**             IPv6: 2607:f8b0:4020:804::2004
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int main(int argc, char* argv[]) {
	struct addrinfo hints;
	struct addrinfo* res, *p;
	int status;
	char ipstr[INET6_ADDRSTRLEN];  // accommodate both IPv4 and IPv6

	if (argc != 2) {
	    fprintf(stderr,"usage: showip hostname\n");
	    exit(1);
	}

	memset(&hints, 0, sizeof(hints));  // initialize struct to empty
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

    // after the call, res will point to a linked list of 1 or more struct addrinfos
	if ((status = getaddrinfo(argv[1], NULL, &hints, &res)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		exit(2);
	}

	printf("IP addresses for %s:\n", argv[1]);

	for(p = res; p != NULL; p = p->ai_next) {
		void* addr;
		char* ipver;

		if (p->ai_family == AF_INET) {
			// find p->ai_addr (of type struct sockaddr*), then convert to ip (of type struct sockaddr_in*)
			struct sockaddr_in* ipv4 = (struct sockaddr_in*)(p->ai_addr);
			addr = &(ipv4->sin_addr);
			ipver = "IPv4";
		} else {
			struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)(p->ai_addr);
			addr = &(ipv6->sin6_addr);
			ipver = "IPv6";
		}

		inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr));  // extract ip string from ip->sin_addr
		printf("  %s: %s\n", ipver, ipstr);
	}

	freeaddrinfo(res);  // free the linked list of struct addrinfos
	return 0;
}
