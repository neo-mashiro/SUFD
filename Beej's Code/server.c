/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define PORT "3490"
#define BACKLOG 10

void sigchld_handler(int s) {
	// this handler function helps reap zombie processes:
	// http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
	(void)s;  // suppress the warning of unused variable
	int saved_errno = errno;  // waitpid() might overwrite errno, so we save and restore it
	while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
	errno = saved_errno;
}

void* get_in_addr(struct sockaddr* sa) {
	return (sa->sa_family == AF_INET)
	? &(((struct sockaddr_in*)sa)->sin_addr)
	: &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void) {
	int status, sockfd, new_fd;
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage cli_addr;
	socklen_t sin_size;
	struct sigaction sa;
	int yes = 1;
	char ipstr[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;  // for server, use local loopback address

	if ((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		return 1;
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;  // bind the first local address we can find
	}

	if (p == NULL) {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	freeaddrinfo(servinfo);

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

    // reap zombie processes that appear as the fork()ed child processes exit
	// this step must be performed before any child processes are spawned
	sa.sa_handler = sigchld_handler;  // register the handler
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {
		sin_size = sizeof(cli_addr);
		new_fd = accept(sockfd, (struct sockaddr*)&cli_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(cli_addr.ss_family, get_in_addr((struct sockaddr*)&cli_addr), ipstr, sizeof(ipstr));
		printf("server: accepted connection from %s\n", ipstr);

		if (!fork()) {  // child process
			close(sockfd);  // child doesn't need the listener
			char* msg = "Hello, client!";
			if (send(new_fd, msg, strlen(msg), 0) == -1) {
				perror("send");
			}
			close(new_fd);
			exit(0);
		}
		close(new_fd);  // parent doesn't need this socket
	}

	return 0;
}
