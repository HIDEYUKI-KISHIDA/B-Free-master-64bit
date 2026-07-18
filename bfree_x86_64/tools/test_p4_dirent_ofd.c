/*
 * P4_DIRENT_OFD — verify directory enumeration cursor is per-OFD.
 *
 * Two independent open() on the same directory must not share a cursor.
 * dup() must share the cursor (same OFD).
 */
#include "fs_ofd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

static ssize_t drain_dents(struct bfree_fs *fs, int fd)
{
	char buf[512];
	ssize_t total = 0;
	ssize_t n;

	for (;;) {
		n = bfree_getdents64(fs, fd, buf, sizeof(buf));
		if (n < 0)
			return -1;
		if (n == 0)
			break;
		total += n;
	}
	return total;
}

static int read_first_batch(struct bfree_fs *fs, int fd, char *name, size_t namesz)
{
	char buf[128];
	struct bfree_linux_dirent64 *de;
	ssize_t n;

	n = bfree_getdents64(fs, fd, buf, sizeof(buf));
	if (n <= 0)
		return -1;
	de = (struct bfree_linux_dirent64 *)buf;
	snprintf(name, namesz, "%s", de->d_name);
	return 0;
}

int main(void)
{
	struct bfree_fs fs;
	int fd_a;
	int fd_b;
	int fd_c;
	char name_a[BFREE_MAX_NAME];
	char name_b[BFREE_MAX_NAME];
	struct bfree_ofd *ofd_a;
	struct bfree_ofd *ofd_b;
	struct bfree_ofd *ofd_c;
	ssize_t full_listing;

	bfree_fs_init(&fs);
	CHECK(bfree_mkdir(&fs, "/tmp/a", 0755) == 0, "mkdir /tmp/a");
	CHECK(bfree_mkdir(&fs, "/tmp/b", 0755) == 0, "mkdir /tmp/b");
	CHECK(bfree_mkdir(&fs, "/tmp/c", 0755) == 0, "mkdir /tmp/c");

	fd_a = bfree_open(&fs, "/tmp", 0, 0);
	fd_b = bfree_open(&fs, "/tmp", 0, 0);
	CHECK(fd_a >= 0 && fd_b >= 0, "open /tmp twice");

	ofd_a = bfree_ofd_for_fd(&fs, fd_a);
	ofd_b = bfree_ofd_for_fd(&fs, fd_b);
	CHECK(ofd_a != NULL && ofd_b != NULL, "resolve OFDs");
	CHECK(ofd_a != ofd_b, "separate open() => separate OFD");

	/* Advance fd_a only; fd_b cursor must stay at 0. */
	CHECK(read_first_batch(&fs, fd_a, name_a, sizeof(name_a)) == 0,
	      "read first dirent batch on fd_a");
	CHECK(ofd_a->dirent_index > 0, "fd_a cursor advanced");
	CHECK(ofd_b->dirent_index == 0, "fd_b cursor still at start");

	CHECK(read_first_batch(&fs, fd_b, name_b, sizeof(name_b)) == 0,
	      "read first dirent batch on fd_b");
	CHECK(strcmp(name_a, name_b) == 0,
	      "both fds see same first entry from independent cursors");

	/* dup shares OFD and cursor. */
	fd_c = bfree_dup(&fs, fd_a);
	ofd_c = bfree_ofd_for_fd(&fs, fd_c);
	CHECK(ofd_c == ofd_a, "dup shares OFD");
	CHECK(ofd_c->dirent_index == ofd_a->dirent_index,
	      "dup shares dirent cursor");

	/* Full listing from a fresh fd. */
	bfree_close(&fs, fd_b);
	fd_b = bfree_open(&fs, "/tmp", 0, 0);
	full_listing = drain_dents(&fs, fd_b);
	CHECK(full_listing > 0, "fresh fd lists /tmp children");

	printf("P4_DIRENT_OFD: PASS\n");
	return 0;
}
