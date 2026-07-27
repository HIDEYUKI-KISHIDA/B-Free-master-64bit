/*
 * P15_ASH_REGRESS_STAGED — ash regress scripts staged in initramfs.
 */
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

static int file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");

	if (f == NULL)
		return 0;
	fclose(f);
	return 1;
}

int main(void)
{
	CHECK(file_exists("guest/initramfs/ash_regress/02_subshell.sh"),
	      "subshell script staged");
	CHECK(file_exists("guest/initramfs/ash_regress/03_cmdsubst.sh"),
	      "cmdsubst script staged");
	CHECK(file_exists("guest/initramfs/bin/busybox"), "busybox staged");

	printf("P15_ASH_REGRESS_STAGED: PASS\n");
	return 0;
}
