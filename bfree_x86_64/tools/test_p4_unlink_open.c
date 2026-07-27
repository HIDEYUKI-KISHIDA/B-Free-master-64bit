/*
 * P4_UNLINK_OPEN — unlink removes name; open fd keeps vnode alive until close.
 */
#include "fs_ofd.h"

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
	struct bfree_vnode *vn;
	int fd;
	char buf[16];
	ssize_t n;

	int seedfd;

	bfree_fs_init(&fs);
	CHECK(bfree_create(&fs, "/tmp/gone.txt", 0644) == 0, "create file");
	seedfd = bfree_open(&fs, "/tmp/gone.txt", 0, 0);
	CHECK(seedfd >= 0, "open for seed");
	CHECK(bfree_write(&fs, seedfd, "hello", 5) == 5, "seed file");
	bfree_close(&fs, seedfd);

	fd = bfree_open(&fs, "/tmp/gone.txt", 0, 0);
	CHECK(fd >= 0, "open file");
	vn = bfree_ofd_for_fd(&fs, fd)->vnode;

	CHECK(bfree_unlink(&fs, "/tmp/gone.txt") == 0, "unlink while open");
	CHECK(bfree_lookup(&fs, "/tmp/gone.txt") == NULL, "path no longer visible");
	CHECK(bfree_vnode_is_alive(vn), "vnode still alive via open fd");

	memset(buf, 0, sizeof(buf));
	n = bfree_read(&fs, fd, buf, sizeof(buf));
	CHECK(n == 5, "read via open fd after unlink");
	CHECK(memcmp(buf, "hello", 5) == 0, "content intact");

	CHECK(bfree_close(&fs, fd) == 0, "close last fd");
	CHECK(!bfree_vnode_is_alive(vn), "vnode reclaimed after close");

	printf("P4_UNLINK_OPEN: PASS\n");
	return 0;
}
