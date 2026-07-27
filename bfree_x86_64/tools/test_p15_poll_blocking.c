/*
 * P15_POLL_BLOCKING — poll timeout spin until pipe data ready.
 */
#include "process.h"
#include "syscall.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

#define INVOKE(nr, a0, a1, a2, a3) \
	bfree_invoke_syscall((nr), (a0), (a1), (a2), (a3), 0, 0)

int main(void)
{
	struct bfree_pollfd pfd;
	int pipefd[2];
	long rc;

	guest_init();

	rc = INVOKE(22, (unsigned long)pipefd, 0, 0, 0);
	CHECK(rc == 0, "pipe");

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = pipefd[0];
	pfd.events = BFREE_POLLIN;
	rc = INVOKE(7, (unsigned long)&pfd, 1, 2, 0);
	CHECK(rc == 0, "poll empty timeout");

	rc = INVOKE(1, (unsigned long)pipefd[1], (unsigned long)"z", 1, 0);
	CHECK(rc == 1, "pipe write");

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = pipefd[0];
	pfd.events = BFREE_POLLIN;
	rc = INVOKE(7, (unsigned long)&pfd, 1, 2, 0);
	CHECK(rc == 1, "poll ready after timeout");
	CHECK((pfd.revents & BFREE_POLLIN) != 0, "poll POLLIN");

	printf("P15_POLL_BLOCKING: PASS\n");
	return 0;
}
