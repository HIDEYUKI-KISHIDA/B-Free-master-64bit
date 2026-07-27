/*
 * P4_MUSL_EXEC — run musl-style static ET_EXEC via host fork/exec shim.
 */
#include "elf_host_run.h"
#include "elf_load.h"
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

#ifndef MUSL_STATIC_ELF_PATH
#define MUSL_STATIC_ELF_PATH "build/host-tests/musl_static.elf"
#endif

static int copy_file_to_fs(struct bfree_fs *fs, const char *host_path,
			   const char *guest_path)
{
	FILE *f;
	char buf[512];
	size_t n;
	int fd;
	size_t off = 0;
	char *data = NULL;
	size_t cap = 0;

	f = fopen(host_path, "rb");
	if (f == NULL)
		return -1;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		if (off + n > cap) {
			size_t nc = cap ? cap * 2 : 4096;
			char *tmp = realloc(data, nc);

			if (tmp == NULL) {
				fclose(f);
				free(data);
				return -1;
			}
			data = tmp;
			cap = nc;
		}
		memcpy(data + off, buf, n);
		off += n;
	}
	fclose(f);

	CHECK(bfree_create(fs, guest_path, 0755) == 0 ||
	      bfree_lookup(fs, guest_path) != NULL, "create guest path");
	fd = bfree_open(fs, guest_path, 0, 0);
	CHECK(fd >= 0, "open guest elf");
	CHECK(bfree_write(fs, fd, data, off) == (ssize_t)off, "write elf bytes");
	bfree_close(fs, fd);
	free(data);
	return 0;
}

int main(void)
{
	struct bfree_fs fs;
	struct bfree_as as;
	struct bfree_elf_image img;
	int status = -1;

	bfree_fs_init(&fs);
	CHECK(copy_file_to_fs(&fs, MUSL_STATIC_ELF_PATH, "/tmp/musl_hello") == 0,
	      "stage musl_static.elf");
	CHECK(bfree_as_init(&as, 65536) == 0, "as init");
	CHECK(bfree_elf_load_path(&fs, "/tmp/musl_hello", &as, &img) == 0,
	      "musl elf load");
	CHECK(bfree_elf_host_exec(&as, img.entry, img.load_size, &status) == 0,
	      "host exec shim");
	CHECK(status == 0, "musl hello exit 0");

	printf("P4_MUSL_EXEC: PASS\n");
	bfree_as_free(&as);
	return 0;
}
