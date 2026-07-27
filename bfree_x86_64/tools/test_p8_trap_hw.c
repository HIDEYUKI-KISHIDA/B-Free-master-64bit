/*
 * P8_TRAP_HW — lidt/wrmsr install skipped on host ring-3.
 */
#include "trap_hw.h"
#include "trap_setup.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	const struct bfree_trap_state *st;
	int rc;

	bfree_trap_init();
	st = bfree_trap_state();
	CHECK(st->syscall_star != 0, "star");
	CHECK(st->syscall_lstar != 0, "lstar");

	rc = bfree_trap_install();
	CHECK(rc == -2, "host install skipped");
	CHECK(bfree_trap_hw_installed() == 0, "not installed");

	printf("P8_TRAP_HW: PASS\n");
	return 0;
}
