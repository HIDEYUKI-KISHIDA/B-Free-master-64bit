/*
 * P5_SYSCALL_GATE — implemented-syscall registry sanity check.
 */
#include "syscall_dispatch.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	int enosys;

	bfree_syscall_registry_init();
	CHECK(bfree_syscall_is_implemented(4), "stat");
	CHECK(bfree_syscall_is_implemented(5), "fstat");
	CHECK(bfree_syscall_is_implemented(7), "poll");
	CHECK(bfree_syscall_is_implemented(262), "fstatat");
	CHECK(bfree_syscall_is_implemented(165), "mount");
	CHECK(bfree_syscall_is_implemented(41), "socket");
	CHECK(bfree_syscall_is_implemented(29), "shmget");
	CHECK(!bfree_syscall_is_implemented(999), "oob nr");
	enosys = bfree_syscall_enosys_count();
	CHECK(enosys > 0 && enosys < 400, "enosys residual bounded");

	printf("P5_SYSCALL_GATE: PASS\n");
	return 0;
}
