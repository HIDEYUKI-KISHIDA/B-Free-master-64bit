/*
 * P4_PIPE_SIGNAL — pipe both ends across children; SIGCHLD + SIGPIPE.
 */
#include "process.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct bfree_proc_mgr mgr;
	int pipefd[2];
	int writer;
	int reader;
	char buf[8];
	ssize_t n;

	bfree_proc_init(&mgr);
	CHECK(bfree_pipe_open(&mgr, pipefd) == 0, "pipe");

	writer = bfree_fork(&mgr);
	CHECK(writer > 0, "fork writer");
	CHECK(bfree_switch_proc(&mgr, writer) == 0, "run writer");
	CHECK(bfree_pipe_write(&mgr, pipefd[1], "ok", 2) == 2, "pipe write");
	bfree_exit(&mgr, 0);

	CHECK(bfree_sig_pending(&mgr, BFREE_SIGCHLD), "parent gets SIGCHLD");

	reader = bfree_fork(&mgr);
	CHECK(reader > 0, "fork reader");
	CHECK(bfree_switch_proc(&mgr, reader) == 0, "run reader");
	memset(buf, 0, sizeof(buf));
	n = bfree_pipe_read(&mgr, pipefd[0], buf, sizeof(buf));
	CHECK(n == 2, "pipe read");
	CHECK(memcmp(buf, "ok", 2) == 0, "pipe payload");
	bfree_exit(&mgr, 0);

	bfree_pipe_close(&mgr, pipefd[0]);
	CHECK(bfree_pipe_write(&mgr, pipefd[1], "x", 1) < 0,
	      "write to closed read end yields EPIPE");
	CHECK(bfree_sig_pending(&mgr, BFREE_SIGPIPE), "SIGPIPE delivered");

	bfree_kill(&mgr, 1, BFREE_SIGINT);
	CHECK(bfree_sig_pending(&mgr, BFREE_SIGINT), "SIGINT delivered");

	printf("P4_PIPE_SIGNAL: PASS\n");
	return 0;
}
