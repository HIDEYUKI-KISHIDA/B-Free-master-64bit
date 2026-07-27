/*
 * P23_PROCFS_BASIC — minimal /proc files for Linux userland init.
 */
#include "fs_ofd.h"
#include "procfs.h"
#include "vmm.h"

#include <stdio.h>
#include <stdint.h>
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
	char buf[2048];
	ssize_t n;
	int fd;

	bfree_fs_init(&fs);
	CHECK(bfree_procfs_init(&fs) == 0, "procfs init");

	fd = bfree_open(&fs, "/proc/meminfo", 0, 0);
	CHECK(fd >= 0, "open meminfo");
	n = bfree_read(&fs, fd, buf, sizeof(buf) - 1U);
	bfree_close(&fs, fd);
	CHECK(n > 0, "read meminfo");
	buf[n] = '\0';
	CHECK(strstr(buf, "MemTotal:") != NULL, "meminfo MemTotal");

	fd = bfree_open(&fs, "/proc/cpuinfo", 0, 0);
	CHECK(fd >= 0, "open cpuinfo");
	n = bfree_read(&fs, fd, buf, sizeof(buf) - 1U);
	bfree_close(&fs, fd);
	CHECK(n > 0, "read cpuinfo");
	buf[n] = '\0';
	CHECK(strstr(buf, "vendor_id") != NULL, "cpuinfo vendor_id");
	CHECK(strstr(buf, "model name") != NULL, "cpuinfo model name");

	fd = bfree_open(&fs, "/proc/self/status", 0, 0);
	CHECK(fd >= 0, "open self/status");
	n = bfree_read(&fs, fd, buf, sizeof(buf) - 1U);
	bfree_close(&fs, fd);
	CHECK(n > 0, "read self/status");
	buf[n] = '\0';
	CHECK(strstr(buf, "Name:\tbusybox") != NULL, "status Name busybox");
	CHECK(strstr(buf, "Threads:\t1") != NULL, "status Threads=1");
	CHECK(strstr(buf, "Uid:\t0\t0\t0\t0") != NULL, "status Uid");

	CHECK(bfree_procfs_on_exec(&fs, "/bin/alt", BFREE_USER_HEAP_BASE,
				      BFREE_USER_VA_STACK) == 0,
	      "procfs on exec");

	fd = bfree_open(&fs, "/proc/self/status", 0, 0);
	CHECK(fd >= 0, "reopen self/status");
	n = bfree_read(&fs, fd, buf, sizeof(buf) - 1U);
	bfree_close(&fs, fd);
	CHECK(n > 0, "read self/status after exec");
	buf[n] = '\0';
	CHECK(strstr(buf, "Name:\talt") != NULL, "status Name alt");

	printf("P23_PROCFS_BASIC: PASS\n");
	return 0;
}

