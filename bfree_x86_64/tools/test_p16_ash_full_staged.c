/*
 * P16_ASH_FULL_STAGED — all ash regress scripts staged in initramfs.
 */
#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

static int file_exists(const char *path)
{
	FILE *f = fopen(path, "r");

	if (f == NULL)
		return 0;
	fclose(f);
	return 1;
}

int main(void)
{
	CHECK(file_exists("guest/initramfs/ash_regress/01_pipe.sh"),
	      "01_pipe.sh staged");
	CHECK(file_exists("guest/initramfs/ash_regress/02_subshell.sh"),
	      "02_subshell.sh staged");
	CHECK(file_exists("guest/initramfs/ash_regress/03_cmdsubst.sh"),
	      "03_cmdsubst.sh staged");
	CHECK(file_exists("guest/initramfs/ash_regress/04_bg.sh"),
	      "04_bg.sh staged");
	CHECK(file_exists("guest/initramfs/ash_regress/05_external.sh"),
	      "05_external.sh staged");
	CHECK(file_exists("guest/initramfs/ash_regress/06_date.sh"),
	      "06_date.sh staged");
	CHECK(file_exists("guest/initramfs/ash_regress/07_id.sh"),
	      "07_id.sh staged");
	CHECK(file_exists("guest/initramfs/ash_regress/08_ln_readlink.sh"),
	      "08_ln_readlink.sh staged");

	printf("P16_ASH_FULL_STAGED: PASS\n");
	return 0;
}
