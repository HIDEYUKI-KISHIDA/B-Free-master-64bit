/*
 * P4_OPENAT — dirfd-relative open/mkdir/unlink; absolute path ignores dirfd.
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
	int tmpfd;
	int relfd;
	int absfd;

	bfree_fs_init(&fs);

	tmpfd = bfree_open(&fs, "/tmp", 0, 0);
	CHECK(tmpfd >= 0, "open /tmp dirfd");

	CHECK(bfree_mkdirat(&fs, tmpfd, "atdir", 0755) == 0, "mkdirat relative");
	CHECK(bfree_lookup(&fs, "/tmp/atdir") != NULL, "relative mkdir visible");

	CHECK(bfree_create(&fs, "/tmp/atdir/inner.txt", 0644) == 0,
	      "create nested file");
	relfd = bfree_openat(&fs, tmpfd, "atdir/inner.txt", 0, 0);
	CHECK(relfd >= 0, "openat nested relative path");

	absfd = bfree_openat(&fs, tmpfd, "/tmp/atdir", 0, 0);
	CHECK(absfd >= 0, "openat absolute ignores dirfd");

	CHECK(bfree_unlinkat(&fs, tmpfd, "atdir/inner.txt", 0) == 0,
	      "unlinkat relative");
	CHECK(bfree_lookup(&fs, "/tmp/atdir/inner.txt") == NULL,
	      "unlinked file gone from namespace");

	bfree_close(&fs, relfd);
	bfree_close(&fs, absfd);
	bfree_close(&fs, tmpfd);

	printf("P4_OPENAT: PASS\n");
	return 0;
}
