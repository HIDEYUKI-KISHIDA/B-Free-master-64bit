/*
 * P4_RW_DUP2 — read/write/lseek, dup2, fcntl FD_CLOEXEC.
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
	char buf[16];
	int fd;
	int fd2;
	int flags;

	bfree_fs_init(&fs);
	bfree_proc_init(&mgr);
	guest_io_init(&io, &fs, &mgr);

	fd = bfree_create(&fs, "/tmp/var/run/testio", 0644);
	CHECK(fd == 0, "create");
	fd = bfree_open(&fs, "/tmp/var/run/testio", 0, 0644);
	CHECK(fd >= 0, "open");
	CHECK(guest_write(&io, fd, "rw-dup2", 7) == 7, "write");
	fd2 = guest_dup2(&io, fd, 5);
	CHECK(fd2 == 5, "dup2 to 5");
	CHECK(guest_lseek(&io, fd2, 0, 0) == 0, "lseek dup2 fd");
	memset(buf, 0, sizeof(buf));
	CHECK(guest_read(&io, fd2, buf, 7) == 7, "read via dup2 fd");
	CHECK(memcmp(buf, "rw-dup2", 7) == 0, "dup2 shared payload");
	CHECK(guest_fcntl(&io, fd, 2, 1) == 0, "fcntl set cloexec");
	flags = guest_fcntl(&io, fd, 1, 0);
	CHECK(flags == 1, "fcntl get cloexec");

	printf("P4_RW_DUP2: PASS\n");
	return 0;
}
