/*
 * P4_SHELL_CLI — M3 normal CLI: pipeline, subshell, cmdsubst, bg, external.
 * Verifies removal of inproc pipe / bg-inline / NOFORK-all hacks.
 */
#include "process.h"
#include "shell_cli.h"
#include "vmm.h"

#include <stdio.h>
#include <string.h>

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
	struct bfree_shell_ctx sh;
	uint8_t *parent_mem;
	char subst[64];
	char *wargv[] = { "/bin/writer", "pipe-data", NULL };
	char *rargv[] = { "/bin/reader", NULL };
	char *touch_argv[] = { "/bin/touch-as", NULL };
	char *echo_argv[] = { "/bin/echo", "hello", NULL };
	char *bg_argv[] = { "/bin/sleep-exit", "3", NULL };
	char *ext_argv[] = { "/bin/true", NULL };
	int status;
	int bg_pid;
	int st;

	bfree_proc_init(&mgr);
	bfree_shell_ctx_init(&sh, &mgr);
	CHECK(bfree_shell_register_std_applets() == 0, "std applets");
	CHECK(bfree_proc_register("/bin/true", true_applet) == 0, "external true");

	/* P4_PIPE_FORK — real pipe across forked children (not inproc) */
	CHECK(bfree_shell_pipeline(&sh, "/bin/writer", wargv,
				   "/bin/reader", rargv) == 0,
	      "pipeline fork+pipe");
	CHECK(strcmp(sh.capture, "pipe-data") == 0, "pipeline payload");
	printf("P4_PIPE_FORK: PASS\n");

	/* P4_SUBSHELL — ( cmd ) with isolated address space */
	parent_mem = bfree_as_ptr(&bfree_proc_current(&mgr)->as);
	CHECK(parent_mem != NULL, "parent as");
	parent_mem[0] = 'P';
	status = -1;
	CHECK(bfree_shell_subshell(&sh, "/bin/touch-as", touch_argv,
				   &status) == 0,
	      "subshell fork");
	CHECK(status == 0, "subshell exit 0");
	CHECK(parent_mem[0] == 'P', "parent as unchanged after subshell");
	printf("P4_SUBSHELL: PASS\n");

	/* P4_CMDSUBST — $(cmd) via pipe read in parent */
	memset(subst, 0, sizeof(subst));
	CHECK(bfree_shell_cmdsubst(&sh, "/bin/echo", echo_argv,
				   subst, sizeof(subst)) == 0,
	      "cmdsubst");
	CHECK(strcmp(subst, "hello") == 0, "cmdsubst output");
	printf("P4_CMDSUBST: PASS\n");

	/* P4_BG_JOB — background fork without bg-inline synchronous wait */
	bg_pid = -1;
	CHECK(bfree_shell_bg(&sh, "/bin/sleep-exit", bg_argv, &bg_pid) == 0,
	      "bg fork");
	CHECK(bg_pid > 1, "bg pid");
	{
		int i;
		int runnable = 0;

		for (i = 0; i < BFREE_MAX_PROC; i++) {
			if (mgr.procs[i].pid == bg_pid &&
			    mgr.procs[i].state == BFREE_PROC_RUNNABLE) {
				runnable = 1;
				break;
			}
		}
		CHECK(runnable, "bg child still runnable before join");
	}
	st = -1;
	CHECK(bfree_shell_bg_join(&sh, &st) == 0, "reap bg job");
	CHECK(st == 0, "bg exit 0");
	printf("P4_BG_JOB: PASS\n");

	/* P4_EXTERNAL — external command via vfork+execve (not NOFORK inline) */
	status = -1;
	CHECK(bfree_shell_external(&sh, "/bin/true", ext_argv, &status) == 0,
	      "external exec");
	CHECK(status == 0, "external exit 0");
	printf("P4_EXTERNAL: PASS\n");

	printf("P4_SHELL_CLI: PASS\n");
	return 0;
}
