/*
 * P13_GUEST_REGRESS — guest LTP/POSIX probe ELFs staged in initramfs.
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

#ifndef LTP_OPEN_BUILT_PATH
#define LTP_OPEN_BUILT_PATH "build/host-tests/ltp_open_guest.elf"
#endif

#ifndef LTP_OPEN_STAGED_PATH
#define LTP_OPEN_STAGED_PATH "guest/initramfs/ltp_open_guest.elf"
#endif

#ifndef POSIX_IO_BUILT_PATH
#define POSIX_IO_BUILT_PATH "build/host-tests/posix_io_guest.elf"
#endif

#ifndef POSIX_IO_STAGED_PATH
#define POSIX_IO_STAGED_PATH "guest/initramfs/posix_io_guest.elf"
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

static int check_elf_pair(const char *built_path, const char *staged_path)
{
	unsigned char *built = NULL;
	unsigned char *staged = NULL;
	size_t built_len = 0;
	size_t staged_len = 0;
	unsigned char e_ident[16];

	if (read_file(built_path, &built, &built_len) != 0)
		return -1;
	if (read_file(staged_path, &staged, &staged_len) != 0) {
		free(built);
		return -1;
	}
	if (built_len != staged_len || memcmp(built, staged, built_len) != 0 ||
	    built_len < 64) {
		free(built);
		free(staged);
		return -1;
	}
	memcpy(e_ident, built, sizeof(e_ident));
	if (e_ident[0] != 0x7f || e_ident[1] != 'E' || e_ident[2] != 'L' ||
	    e_ident[3] != 'F' || e_ident[4] != 2 ||
	    *(unsigned short *)(built + 16) != 2) {
		free(built);
		free(staged);
		return -1;
	}

	free(built);
	free(staged);
	return 0;
}

int main(void)
{
	CHECK(check_elf_pair(LTP_OPEN_BUILT_PATH, LTP_OPEN_STAGED_PATH) == 0,
	      "ltp_open_guest staged");
	CHECK(check_elf_pair(POSIX_IO_BUILT_PATH, POSIX_IO_STAGED_PATH) == 0,
	      "posix_io_guest staged");

	printf("P13_GUEST_REGRESS: PASS\n");
	return 0;
}
