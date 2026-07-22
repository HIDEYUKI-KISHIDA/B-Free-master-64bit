/*
 * P10_KERNEL_GUEST — freestanding guest_init + stdio console wiring.
 */
#include "guest_kernel.h"
#include "syscall.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	long rc;

	bfree_kernel_guest_init();
	rc = bfree_invoke_syscall(39, 0, 0, 0, 0, 0, 0);
	CHECK(rc == 1, "getpid after kernel guest init");
	rc = bfree_invoke_syscall(1, 1, (unsigned long)"P10_KERNEL_GUEST\n", 18, 0, 0, 0);
	CHECK(rc == 18, "write stdout via /dev/console");

	printf("P10_KERNEL_GUEST: PASS\n");
	return 0;
}
