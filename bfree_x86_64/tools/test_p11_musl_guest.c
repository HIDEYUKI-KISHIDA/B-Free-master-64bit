/*
 * P11_MUSL_GUEST — musl static ELF staged in initramfs for guest boot.
 */
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

#ifndef MUSL_INITRAMFS_ELF_PATH
#define MUSL_INITRAMFS_ELF_PATH "guest/initramfs/musl_static.elf"
#endif

static int read_file(const char *path, unsigned char **out, size_t *out_len)
{
	FILE *f;
	long sz;
	unsigned char *buf;

	f = fopen(path, "rb");
	if (f == NULL)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return -1;
	}
	rewind(f);
	buf = malloc((size_t)sz);
	if (buf == NULL) {
		fclose(f);
		return -1;
	}
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(buf);
		fclose(f);
		return -1;
	}
	fclose(f);
	*out = buf;
	*out_len = (size_t)sz;
	return 0;
}

int main(void)
{
	unsigned char *built = NULL;
	unsigned char *staged = NULL;
	size_t built_len = 0;
	size_t staged_len = 0;
	unsigned char e_ident[16];

	CHECK(read_file(MUSL_STATIC_ELF_PATH, &built, &built_len) == 0, "built musl");
	CHECK(read_file(MUSL_INITRAMFS_ELF_PATH, &staged, &staged_len) == 0,
	      "initramfs musl");
	CHECK(built_len == staged_len, "staged size");
	CHECK(memcmp(built, staged, built_len) == 0, "staged bytes");
	CHECK(built_len >= 64, "elf size");
	memcpy(e_ident, built, sizeof(e_ident));
	CHECK(e_ident[0] == 0x7f && e_ident[1] == 'E' && e_ident[2] == 'L' &&
	      e_ident[3] == 'F', "elf magic");
	CHECK(e_ident[4] == 2, "elf64");
	CHECK(*(unsigned short *)(built + 16) == 2, "ET_EXEC");
	CHECK(*(unsigned long *)(built + 24) == 0x1024UL, "entry");

	free(built);
	free(staged);
	printf("P11_MUSL_GUEST: PASS\n");
	return 0;
}
