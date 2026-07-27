/*
 * P9_USER_BOOT — payload install skipped on host harness.
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
	char blob[4] = { 't', 'e', 's', 't' };

	CHECK(bfree_user_payload_install(NULL, 0) == -1, "null");
	CHECK(bfree_user_payload_install(blob, sizeof(blob)) == -2, "host skip");

	printf("P9_USER_BOOT: PASS\n");
	return 0;
}
