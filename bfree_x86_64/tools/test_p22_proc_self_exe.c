/*
 * P22_PROC_SELF_EXE — Linux-on-BTRON L4: /proc/self/exe + maps.
 */
#include "fs_ofd.h"
#include "procfs.h"
#include "vmm.h"

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
	char buf[256];
	char maps[512];
	ssize_t n;
	int fd;

	bfree_fs_init(&fs);
	CHECK(bfree_procfs_init(&fs) == 0, "procfs init");

	n = bfree_readlink(&fs, "/proc/self/exe", buf, sizeof(buf) - 1);
	CHECK(n > 0, "readlink exe");
	buf[n] = '\0';
	CHECK(strcmp(buf, "/bin/busybox") == 0, "exe -> busybox");

	fd = bfree_open(&fs, "/proc/self/maps", 0, 0);
	CHECK(fd >= 0, "open maps");
	n = bfree_read(&fs, fd, maps, sizeof(maps) - 1);
	bfree_close(&fs, fd);
	CHECK(n > 0, "maps non-empty");
	maps[n] = '\0';
	CHECK(strstr(maps, "/bin/busybox") != NULL, "maps names busybox");

	CHECK(bfree_mkdir(&fs, "/bin", 0755) == 0 ||
	      bfree_lookup(&fs, "/bin") != NULL, "bin dir");
	CHECK(bfree_create(&fs, "/bin/alt", 0755) == 0 ||
	      bfree_lookup(&fs, "/bin/alt") != NULL, "alt file");
	CHECK(bfree_procfs_on_exec(&fs, "/bin/alt", BFREE_USER_HEAP_BASE,
				   BFREE_USER_VA_STACK) == 0,
	      "procfs on exec");

	n = bfree_readlink(&fs, "/proc/self/exe", buf, sizeof(buf) - 1);
	CHECK(n > 0, "readlink after exec");
	buf[n] = '\0';
	CHECK(strcmp(buf, "/bin/alt") == 0, "exe updated");

	fd = bfree_open(&fs, "/proc/self/maps", 0, 0);
	CHECK(fd >= 0, "reopen maps");
	n = bfree_read(&fs, fd, maps, sizeof(maps) - 1);
	bfree_close(&fs, fd);
	CHECK(n > 0, "maps after exec");
	maps[n] = '\0';
	CHECK(strstr(maps, "/bin/alt") != NULL, "maps names alt");
	CHECK(strstr(maps, "[heap]") != NULL, "maps has heap");
	CHECK(strstr(maps, "[stack]") != NULL, "maps has stack");

	printf("P22_PROC_SELF_EXE: PASS\n");
	return 0;
}
