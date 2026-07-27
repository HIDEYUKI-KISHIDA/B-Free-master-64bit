/*
 * P16_PIPE_DUP2 — dup2 on kernel pipe fds (ash pipeline wiring).
 */
#include "fs_ofd.h"
#include "guest_io.h"
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
	struct bfree_fs fs;
	struct bfree_proc_mgr mgr;
	struct guest_io io;
	int pipefd[2];
	int stdout_fd = 1;
	char buf[16];
	ssize_t n;

	bfree_fs_init(&fs);
	bfree_proc_init(&mgr);
	guest_io_init(&io, &fs, &mgr);

	CHECK(bfree_pipe_open(&mgr, pipefd) == 0, "pipe");
	CHECK(guest_dup2(&io, pipefd[1], stdout_fd) == stdout_fd, "dup2 pipe write");
	CHECK(guest_write(&io, stdout_fd, "pipe-data", 9) == 9, "write via dup2");
	CHECK(guest_dup2(&io, pipefd[0], 0) == 0, "dup2 pipe read");
	memset(buf, 0, sizeof(buf));
	n = guest_read(&io, 0, buf, sizeof(buf) - 1);
	CHECK(n == 9, "read via dup2 stdin");
	CHECK(memcmp(buf, "pipe-data", 9) == 0, "pipe payload");

	printf("P16_PIPE_DUP2: PASS\n");
	return 0;
}
