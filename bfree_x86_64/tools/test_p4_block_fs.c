/*
 * P4_BLOCK_FS — persistent block volume survives unmount/remount.
 */
#include "blk_vol.h"
#include "fs_ofd.h"

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
	const char *img = "/tmp/bfree_p4_block_fs.img";
	struct bfree_fs fs;
	struct bfree_fs fs2;
	int fd;
	char buf[32];

	unlink(img);

	CHECK(bfree_blk_format(img, 128) == 0, "format block volume");

	bfree_fs_init(&fs);
	CHECK(bfree_fs_mount(&fs, img) == 0, "mount fresh volume");
	CHECK(bfree_create(&fs, "/persist.txt", 0644) == 0, "create file");
	fd = bfree_open(&fs, "/persist.txt", 0, 0);
	CHECK(fd >= 0, "open for write");
	CHECK(bfree_write(&fs, fd, "block-data", 10) == 10, "block write");
	bfree_close(&fs, fd);
	CHECK(bfree_fs_sync(&fs) == 0, "sync to disk");
	bfree_fs_umount(&fs);

	bfree_fs_init(&fs2);
	CHECK(bfree_fs_mount(&fs2, img) == 0, "remount volume");
	CHECK(bfree_lookup(&fs2, "/persist.txt") != NULL, "file exists after remount");
	fd = bfree_open(&fs2, "/persist.txt", 0, 0);
	CHECK(fd >= 0, "reopen persisted file");
	memset(buf, 0, sizeof(buf));
	CHECK(bfree_read(&fs2, fd, buf, sizeof(buf)) == 10, "read persisted data");
	CHECK(memcmp(buf, "block-data", 10) == 0, "data matches");
	bfree_close(&fs2, fd);
	bfree_fs_umount(&fs2);
	unlink(img);

	printf("P4_BLOCK_FS: PASS\n");
	return 0;
}
