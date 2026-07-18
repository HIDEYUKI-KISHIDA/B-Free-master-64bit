/*
 * P4_VFORK_EXEC — child-first vfork, execve registered applet, wait4 reap.
 */
#include "process.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

static int true_applet(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	return 0;
}

int main(void)
{
	struct bfree_proc_mgr mgr;
	int pid;
	int status = -1;

	bfree_proc_init(&mgr);
	bfree_proc_register("/bin/true", true_applet);

	pid = bfree_spawn_vfork_child(&mgr, "/bin/true", NULL, NULL);
	CHECK(pid > 1, "spawn vfork child returns pid");
	CHECK(bfree_wait4(&mgr, pid, &status, 0, NULL) == pid, "wait4 reaps child");
	CHECK(status == 0, "child exit status 0");
	CHECK(bfree_proc_zombie_count(&mgr) == 0, "no zombies left");

	printf("P4_VFORK_EXEC: PASS\n");
	return 0;
}
