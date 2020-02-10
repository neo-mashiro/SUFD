/*
** poll.c -- an example of poll()
*/

#include <stdio.h>
#include <poll.h>

int main(void) {
	struct pollfd pfds[1];  // monitor 1 socket

	pfds[0].fd = 0;           // socket descriptor
	pfds[0].events = POLLIN;  // let me know when data is ready to recv()

	printf("Hit Enter or wait 2.5 seconds for timeout\n");

	int num_events = poll(pfds, 1, 2500); // 2.5 seconds timeout

	if (num_events == 0) {
		printf("Poll timed out!\n");
	} else {
		int pollin_happened = pfds[0].revents & POLLIN;
		if (pollin_happened) {
			printf("File descriptor %d is ready to read\n", pfds[0].fd);
		} else {
			printf("Unexpected event occurred: %d\n", pfds[0].revents);
		}
	}

	return 0;
}
