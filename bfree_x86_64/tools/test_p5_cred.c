/*
 * P5_CRED — uid/gid get/set as root.
 */
#include "cred.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	CHECK(bfree_getuid() == 0, "root uid");
	CHECK(bfree_geteuid() == 0, "root euid");
	CHECK(bfree_setgid(2000) == 0, "setgid");
	CHECK(bfree_getgid() == 2000, "gid changed");
	CHECK(bfree_setuid(1000) == 0, "setuid");
	CHECK(bfree_getuid() == 1000 && bfree_geteuid() == 1000, "uid/euid dropped");

	printf("P5_CRED: PASS\n");
	return 0;
}
