/*
 * P10_DISPATCH_WIRING — M10 syscall dispatch gaps closed.
 */
#include "fs_ofd.h"
#include "syscall.h"
#include "syscall_dispatch.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

#define INVOKE(nr, a0, a1, a2) \
	bfree_invoke_syscall((nr), (a0), (a1), (a2), 0, 0, 0)

int main(void)
{
	struct bfree_fs *fs;
	long fd;
	long dupfd;
	long rc;

	guest_init();
	fs = guest_fs();

	bfree_syscall_registry_init();
	CHECK(bfree_syscall_is_implemented(257), "openat registered");
	CHECK(bfree_syscall_is_implemented(217), "getdents64 registered");
	CHECK(bfree_syscall_is_implemented(32), "dup registered");
	CHECK(bfree_syscall_is_implemented(39), "getpid registered");

	CHECK(bfree_create(fs, "/tmp/m10.txt", 0644) == 0, "create");
	fd = INVOKE(257, (unsigned long)BFREE_AT_FDCWD,
		    (unsigned long)"/tmp/m10.txt", 0);
	CHECK(fd >= 0, "openat");
	dupfd = INVOKE(32, (unsigned long)fd, 0, 0);
	CHECK(dupfd >= 0, "dup");
	rc = INVOKE(1, (unsigned long)dupfd, (unsigned long)"m10", 3);
	CHECK(rc == 3, "write dup fd");
	rc = INVOKE(39, 0, 0, 0);
	CHECK(rc == 1, "getpid");
	rc = INVOKE(110, 0, 0, 0);
	CHECK(rc == 0, "getppid");

	printf("P10_DISPATCH_WIRING: PASS\n");
	return 0;
}
