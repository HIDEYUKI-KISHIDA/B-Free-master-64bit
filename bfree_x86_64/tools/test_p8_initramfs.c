/*
 * P8_INITRAMFS — newc cpio parse and lookup.
 */
#include "initramfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	FILE *fp;
	char *buf;
	long sz;
	const void *data;
	size_t size;

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

	CHECK(bfree_initramfs_parse(buf, (size_t)sz) == 0, "parse");
	CHECK(bfree_initramfs_file_count() >= 1, "count");
	CHECK(bfree_initramfs_lookup("hello.txt", &data, &size) == 0, "lookup");
	CHECK(size > 0, "size");
	CHECK(memcmp(data, "hello", 5) == 0, "payload");

	free(buf);
	printf("P8_INITRAMFS: PASS\n");
	return 0;
}
