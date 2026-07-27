/*
 * P6_SYSCALL_INVOKE — runtime dispatch to sys_* via bfree_invoke_syscall.
 */
#include "fs_ofd.h"
#include "syscall.h"

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
	char buf[32];
	long fd;
	long rc;

	guest_init();
	fs = guest_fs();

	CHECK(bfree_create(fs, "/tmp/invoke.txt", 0644) == 0, "create");
	fd = INVOKE(2, (unsigned long)"/tmp/invoke.txt", 0, 0644);
	CHECK(fd >= 0, "invoke open");
	rc = INVOKE(1, (unsigned long)fd, (unsigned long)"invoke", 6);
	CHECK(rc == 6, "invoke write");
	rc = INVOKE(8, (unsigned long)fd, 0, 0);
	CHECK(rc == 0, "invoke lseek");
	memset(buf, 0, sizeof(buf));
	rc = INVOKE(0, (unsigned long)fd, (unsigned long)buf, 6);
	CHECK(rc == 6, "invoke read");
	CHECK(memcmp(buf, "invoke", 6) == 0, "payload");
	rc = INVOKE(3, (unsigned long)fd, 0, 0);
	CHECK(rc == 0, "invoke close");
	rc = INVOKE(102, 0, 0, 0);
	CHECK(rc == 0, "invoke getuid");
	rc = INVOKE(999, 0, 0, 0);
	CHECK(rc == -ENOSYS, "invoke enosys");

	printf("P6_SYSCALL_INVOKE: PASS\n");
	return 0;
}
