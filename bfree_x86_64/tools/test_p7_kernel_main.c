/*
 * P7_KERNEL_MAIN — hosted kernel_main with trap init + invoke write.
 */
#include "kernel_main.h"
#include "syscall.h"
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
	CHECK(bfree_trap_is_ready() == 0, "not ready before init");
	bfree_kernel_main();
	CHECK(bfree_trap_is_ready() != 0, "trap ready after kernel_main");
	printf("P7_KERNEL_MAIN: PASS\n");
	return 0;
}
