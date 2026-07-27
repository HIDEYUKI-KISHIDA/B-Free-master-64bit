/*
 * P5_MOUNT — mount/umount2 wrapper over persistent block FS.
 */
#include "blk_vol.h"
#include "fs_ofd.h"
#include "mount.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	const char *img = "/tmp/bfree_p5_mount.img";
	struct bfree_fs fs;
	int fd;
	char buf[16];

	unlink(img);
	CHECK(bfree_blk_format(img, 128) == 0, "format");

	bfree_fs_init(&fs);
	CHECK(bfree_mount(&fs, "/", img) == 0, "mount");
	CHECK(bfree_create(&fs, "/mntfile", 0644) == 0, "create on mount");
	fd = bfree_open(&fs, "/mntfile", 0, 0);
	CHECK(fd >= 0, "open");
	CHECK(bfree_write(&fs, fd, "mounted", 7) == 7, "write");
	bfree_close(&fs, fd);
	CHECK(bfree_fs_sync(&fs) == 0, "sync");
	CHECK(bfree_umount2(&fs, "/", 0) == 0, "umount2");

	CHECK(bfree_mount(&fs, "/", img) == 0, "remount");
	fd = bfree_open(&fs, "/mntfile", 0, 0);
	CHECK(fd >= 0, "reopen");
	memset(buf, 0, sizeof(buf));
	CHECK(bfree_read(&fs, fd, buf, 7) == 7, "read back");
	CHECK(memcmp(buf, "mounted", 7) == 0, "data intact");
	bfree_close(&fs, fd);
	bfree_umount2(&fs, "/", 0);
	unlink(img);

	printf("P5_MOUNT: PASS\n");
	return 0;
}
