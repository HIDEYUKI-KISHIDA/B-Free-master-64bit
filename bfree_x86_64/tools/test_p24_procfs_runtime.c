/*
 * P24_PROCFS_RUNTIME — L6 procfs runtime-ish files.
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

static int has_auxv_tag(const uint64_t *av, size_t n, uint64_t tag)
{
	size_t i;

	for (i = 0; i + 1 < n; i += 2) {
		if (av[i] == tag)
			return 1;
		if (av[i] == 0)
			break;
	}
	return 0;
}

int main(void)
{
	struct bfree_fs fs;
	char buf[1024];
	uint64_t auxv[32];
	ssize_t n;
	int fd;

	bfree_fs_init(&fs);
	CHECK(bfree_procfs_init(&fs) == 0, "procfs init");

	fd = bfree_open(&fs, "/proc/uptime", 0, 0);
	CHECK(fd >= 0, "open uptime");
	n = bfree_read(&fs, fd, buf, sizeof(buf) - 1U);
	bfree_close(&fs, fd);
	CHECK(n > 0, "read uptime");
	buf[n] = '\0';
	CHECK(strstr(buf, ".") != NULL, "uptime decimal");

	fd = bfree_open(&fs, "/proc/self/stat", 0, 0);
	CHECK(fd >= 0, "open self/stat");
	n = bfree_read(&fs, fd, buf, sizeof(buf) - 1U);
	bfree_close(&fs, fd);
	CHECK(n > 0, "read self/stat");
	buf[n] = '\0';
	CHECK(strstr(buf, "(busybox)") != NULL, "stat name busybox");

	fd = bfree_open(&fs, "/proc/self/auxv", 0, 0);
	CHECK(fd >= 0, "open self/auxv");
	n = bfree_read(&fs, fd, auxv, sizeof(auxv));
	bfree_close(&fs, fd);
	CHECK(n > 0, "read self/auxv");
	CHECK((n % (ssize_t)sizeof(uint64_t)) == 0, "auxv u64 aligned");
	CHECK(has_auxv_tag(auxv, (size_t)n / sizeof(uint64_t), 6), "AT_PAGESZ");
	CHECK(has_auxv_tag(auxv, (size_t)n / sizeof(uint64_t), 11), "AT_UID");

	CHECK(bfree_procfs_on_exec(&fs, "/bin/sh", BFREE_USER_HEAP_BASE,
				   BFREE_USER_VA_STACK) == 0, "procfs on exec");

	fd = bfree_open(&fs, "/proc/self/stat", 0, 0);
	CHECK(fd >= 0, "reopen self/stat");
	n = bfree_read(&fs, fd, buf, sizeof(buf) - 1U);
	bfree_close(&fs, fd);
	CHECK(n > 0, "reread self/stat");
	buf[n] = '\0';
	CHECK(strstr(buf, "(sh)") != NULL, "stat name sh");

	printf("P24_PROCFS_RUNTIME: PASS\n");
	return 0;
}

