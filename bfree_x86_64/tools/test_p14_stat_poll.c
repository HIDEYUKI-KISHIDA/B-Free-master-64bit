/*
 * P14_STAT_POLL — stat/fstat/fstatat/poll via bfree_invoke_syscall.
 */
#include "fs_ofd.h"
#include "process.h"
#include "syscall.h"

#include <errno.h>
#include <fcntl.h>
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

#define S_IFCHR 0020000
#define S_IFREG 0100000

int main(void)
{
	struct bfree_linux_stat st;
	struct bfree_pollfd pfd;
	int pipefd[2];
	long fd;
	long rc;

	guest_init();

	CHECK(bfree_create(guest_fs(), "/tmp/stat.txt", 0644) == 0, "create");
	fd = INVOKE(2, (unsigned long)"/tmp/stat.txt", O_RDWR, 0644, 0);
	CHECK(fd >= 0, "open");

	memset(&st, 0, sizeof(st));
	rc = INVOKE(4, (unsigned long)"/tmp/stat.txt", (unsigned long)&st, 0, 0);
	CHECK(rc == 0, "stat");
	CHECK((st.st_mode & 0170000) == S_IFREG, "stat mode reg");
	CHECK(st.st_size == 0, "stat size");

	memset(&st, 0, sizeof(st));
	rc = INVOKE(5, (unsigned long)fd, (unsigned long)&st, 0, 0);
	CHECK(rc == 0, "fstat");
	CHECK((st.st_mode & 0170000) == S_IFREG, "fstat mode reg");

	memset(&st, 0, sizeof(st));
	rc = INVOKE(262, (unsigned long)BFREE_AT_FDCWD,
		    (unsigned long)"/tmp/stat.txt", (unsigned long)&st, 0);
	CHECK(rc == 0, "fstatat");
	CHECK((st.st_mode & 0170000) == S_IFREG, "fstatat mode reg");

	memset(&st, 0, sizeof(st));
	rc = INVOKE(4, (unsigned long)"/dev/console", (unsigned long)&st, 0, 0);
	CHECK(rc == 0, "stat dev");
	CHECK((st.st_mode & 0170000) == S_IFCHR, "stat dev chr");

	rc = INVOKE(22, (unsigned long)pipefd, 0, 0, 0);
	CHECK(rc == 0, "pipe");
	rc = INVOKE(1, (unsigned long)pipefd[1], (unsigned long)"x", 1, 0);
	CHECK(rc == 1, "pipe write");

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = pipefd[0];
	pfd.events = BFREE_POLLIN;
	rc = INVOKE(7, (unsigned long)&pfd, 1, 0, 0);
	CHECK(rc == 1, "poll ready");
	CHECK((pfd.revents & BFREE_POLLIN) != 0, "poll POLLIN");

	rc = INVOKE(3, (unsigned long)pipefd[0], 0, 0, 0);
	CHECK(rc == 0, "close read");
	rc = INVOKE(3, (unsigned long)pipefd[1], 0, 0, 0);
	CHECK(rc == 0, "close write");
	rc = INVOKE(3, (unsigned long)fd, 0, 0, 0);
	CHECK(rc == 0, "close file");

	rc = INVOKE(999, 0, 0, 0, 0);
	CHECK(rc == -ENOSYS, "enosys");

	printf("P14_STAT_POLL: PASS\n");
	return 0;
}
