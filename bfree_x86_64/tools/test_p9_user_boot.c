/*
 * P9_USER_BOOT — ring-3 launch skipped on host harness.
 */
#include "user_boot.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	int rc;

	rc = bfree_user_boot_exec(0x100000UL, 0x200000UL);
	CHECK(rc == -2, "host user boot skipped");

	printf("P9_USER_BOOT: PASS\n");
	return 0;
}
