/*
 * P21_MULTI_MB_EXECVE — Linux-on-BTRON L3: multi-MB blob + symlink resolve.
 */
#include "elf_user_exec.h"
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
	struct bfree_vnode *vn = NULL;
	char resolved[BFREE_MAX_PATH];
	int fd;

	CHECK(bfree_user_exec_blob_max() >= (3U * 1024U * 1024U),
	      "blob max >= 3MiB");
	CHECK(bfree_user_exec_blob_max() == BFREE_USER_EXEC_BLOB_MAX,
	      "blob max matches header");

	bfree_fs_init(&fs);
	CHECK(bfree_mkdir(&fs, "/bin", 0755) == 0, "mkdir /bin");
	CHECK(bfree_create(&fs, "/bin/busybox", 0755) == 0, "create busybox");
	fd = bfree_open(&fs, "/bin/busybox", 0, 0);
	CHECK(fd >= 0, "open busybox");
	CHECK(bfree_write(&fs, fd, "ELF", 3) == 3, "write stub");
	bfree_close(&fs, fd);

	CHECK(bfree_mkdir(&fs, "/proc", 0755) == 0, "mkdir /proc");
	CHECK(bfree_mkdir(&fs, "/proc/self", 0755) == 0, "mkdir /proc/self");
	CHECK(bfree_symlink(&fs, "/bin/busybox", "/proc/self/exe") == 0,
	      "symlink exe");

	CHECK(bfree_user_exec_resolve(&fs, "/proc/self/exe", resolved,
				      sizeof(resolved), &vn) == 0,
	      "resolve /proc/self/exe");
	CHECK(vn != NULL && vn->type == BFREE_VNODE_FILE, "resolved file");
	CHECK(strcmp(resolved, "/bin/busybox") == 0, "resolved path");

	CHECK(bfree_user_exec_resolve(&fs, "/bin/busybox", resolved,
				      sizeof(resolved), &vn) == 0,
	      "resolve direct path");
	CHECK(strcmp(resolved, "/bin/busybox") == 0, "direct path");

	printf("P21_MULTI_MB_EXECVE: PASS\n");
	return 0;
}
