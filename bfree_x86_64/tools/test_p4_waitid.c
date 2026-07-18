/*
 * P4_WAITID — multiple zombie children, waitid P_PID / P_ALL + WNOHANG.
 */
#include "process.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct bfree_proc_mgr mgr;
	int status;
	int pid_a;
	int pid_b;

	bfree_proc_init(&mgr);

	pid_a = bfree_fork(&mgr);
	CHECK(pid_a > 0, "fork child A");
	CHECK(bfree_switch_proc(&mgr, pid_a) == 0, "switch to child A");
	bfree_exit(&mgr, 11);

	pid_b = bfree_fork(&mgr);
	CHECK(pid_b > 0, "fork child B");
	CHECK(bfree_switch_proc(&mgr, pid_b) == 0, "switch to child B");
	bfree_exit(&mgr, 22);

	CHECK(bfree_proc_zombie_count(&mgr) == 2, "two zombies pending");

	status = -1;
	CHECK(bfree_waitid(&mgr, P_PID, pid_a, &status, WEXITED) == pid_a,
	      "waitid P_PID reaps first child");
	CHECK(status == 11, "first child status");

	status = -1;
	CHECK(bfree_waitid(&mgr, P_ALL, pid_b, &status, WNOHANG | WEXITED) == 0,
	      "waitid WNOHANG succeeds");
	CHECK(status == 22, "second child status");
	CHECK(bfree_proc_zombie_count(&mgr) == 0, "all zombies reaped");

	printf("P4_WAITID: PASS\n");
	return 0;
}
