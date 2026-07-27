/*
 * P5_DEVNODE — /dev/null and /dev/zero read/write semantics.
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
	char buf[16];
	int fd;

	bfree_fs_init(&fs);

	fd = bfree_open(&fs, "/dev/null", 0, 0);
	CHECK(fd >= 0, "open /dev/null");
	CHECK(bfree_read(&fs, fd, buf, sizeof(buf)) == 0, "null read eof");
	CHECK(bfree_write(&fs, fd, "discard", 7) == 7, "null write");
	bfree_close(&fs, fd);

	fd = bfree_open(&fs, "/dev/zero", 0, 0);
	CHECK(fd >= 0, "open /dev/zero");
	memset(buf, 0xAA, sizeof(buf));
	CHECK(bfree_read(&fs, fd, buf, 8) == 8, "zero read");
	CHECK(buf[0] == 0 && buf[7] == 0, "zero bytes");
	bfree_close(&fs, fd);

	printf("P5_DEVNODE: PASS\n");
	return 0;
}
