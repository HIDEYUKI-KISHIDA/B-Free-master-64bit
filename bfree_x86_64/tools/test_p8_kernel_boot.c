/*
 * P8_KERNEL_BOOT — host harness for freestanding boot path logic.
 */
#include "initramfs.h"
#include "paging.h"
#include "trap_hw.h"
#include "trap_setup.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

#ifndef INITRAMFS_CPIO_PATH
#define INITRAMFS_CPIO_PATH "build/initramfs.cpio"
#endif

int main(void)
{
	struct bfree_paging_state pg;
	FILE *fp;
	char *buf;
	long sz;

	bfree_paging_build_identity(&pg);
	CHECK(bfree_paging_install(&pg) == -2, "paging skip");
	bfree_trap_init();
	CHECK(bfree_trap_install() == -2, "trap skip");

	fp = fopen(INITRAMFS_CPIO_PATH, "rb");
	CHECK(fp != NULL, "open cpio");
	CHECK(fseek(fp, 0, SEEK_END) == 0, "seek end");
	sz = ftell(fp);
	CHECK(sz > 0, "size");
	CHECK(fseek(fp, 0, SEEK_SET) == 0, "seek set");
	buf = malloc((size_t)sz);
	CHECK(buf != NULL, "malloc");
	CHECK(fread(buf, 1, (size_t)sz, fp) == (size_t)sz, "read");
	fclose(fp);

	CHECK(bfree_initramfs_parse(buf, (size_t)sz) == 0, "initramfs");
	CHECK(bfree_initramfs_file_count() > 0, "files");
	free(buf);

	printf("P8_KERNEL_BOOT: PASS\n");
	return 0;
}
