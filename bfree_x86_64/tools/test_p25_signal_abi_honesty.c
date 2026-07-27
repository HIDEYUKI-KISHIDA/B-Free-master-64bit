/*
 * P25_SIGNAL_ABI_HONESTY — L7 signal ABI tightening.
 */
#include "signal_frame.h"
#include "syscall.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct bfree_sigaction_abi act;
	struct bfree_sigaction_abi oact;
	unsigned long mask = 0xA5A5UL;
	unsigned long old = 0;
	long rc;

	guest_init();
	memset(&act, 0, sizeof(act));
	memset(&oact, 0, sizeof(oact));
	act.handler = 0x12345678UL;

	rc = bfree_invoke_syscall(13, 2, (unsigned long)&act,
				  (unsigned long)&oact, sizeof(unsigned long), 0, 0);
	CHECK(rc == 0, "rt_sigaction basic");
	CHECK(oact.handler == 0, "old handler default");

	rc = bfree_invoke_syscall(13, 2, (unsigned long)&act,
				  (unsigned long)&oact, 4, 0, 0);
	CHECK(rc == -EINVAL, "rt_sigaction rejects bad sigsetsize");

	rc = bfree_invoke_syscall(14, 2, (unsigned long)&mask,
				  (unsigned long)&old, sizeof(unsigned long), 0, 0);
	CHECK(rc == 0, "rt_sigprocmask setmask");

	old = 0;
	rc = bfree_invoke_syscall(14, 0, 0,
				  (unsigned long)&old, sizeof(unsigned long), 0, 0);
	CHECK(rc == 0, "rt_sigprocmask query");
	CHECK(old == mask, "rt_sigprocmask stores mask");

	rc = bfree_invoke_syscall(14, 0, 0,
				  (unsigned long)&old, 4, 0, 0);
	CHECK(rc == -EINVAL, "rt_sigprocmask rejects bad sigsetsize");

	rc = bfree_invoke_syscall(15, 0, 0, 0, 0, 0, 0);
	CHECK(rc == -EFAULT, "rt_sigreturn without frame is honest");

	printf("P25_SIGNAL_ABI_HONESTY: PASS\n");
	return 0;
}
