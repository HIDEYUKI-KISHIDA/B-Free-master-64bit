/*
 * P4_FORK — eager-copy address space; parent and child are isolated.
 */
#include "process.h"
#include "vmm.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct bfree_proc_mgr mgr;
	uint8_t *parent_mem;
	uint8_t *child_mem;
	int pid;
	int status;

	bfree_proc_init(&mgr);
	parent_mem = bfree_as_ptr(&bfree_proc_current(&mgr)->as);
	CHECK(parent_mem != NULL, "parent as");
	parent_mem[0] = 'P';

	pid = bfree_fork(&mgr);
	CHECK(pid > 1, "fork returns child pid to parent");

	CHECK(bfree_switch_proc(&mgr, pid) == 0, "enter child");
	child_mem = bfree_as_ptr(&bfree_proc_current(&mgr)->as);
	CHECK(child_mem != NULL, "child as");
	CHECK(child_mem[0] == 'P', "child inherits parent snapshot");
	child_mem[0] = 'C';
	bfree_exit(&mgr, 0);

	CHECK(parent_mem[0] == 'P', "parent memory unchanged after child write");
	status = -1;
	CHECK(bfree_wait4(&mgr, pid, &status, 0, NULL) == pid, "wait4 child");
	CHECK(status == 0, "child exit 0");

	printf("P4_FORK: PASS\n");
	return 0;
}
