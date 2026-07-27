/*
 * P4_TTY_JOB — setsid, setpgid, tcsetpgrp foreground group.
 */
#include "process.h"
#include "tty.h"

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
	int sid;
	int pg;

	bfree_proc_init(&mgr);

	sid = bfree_setsid(&mgr);
	CHECK(sid > 0, "setsid");
	CHECK(bfree_proc_current(&mgr)->pgid == sid, "leader pgid");

	CHECK(bfree_setpgid(&mgr, 0, 0) == 0, "setpgid self");
	pg = bfree_getpgid(&mgr, 0);
	CHECK(pg == sid, "getpgid self");
	CHECK(bfree_tcsetpgrp(0, sid) == 0, "tcsetpgrp");
	CHECK(bfree_tcgetpgrp(0) == sid, "tcgetpgrp");

	printf("P4_TTY_JOB: PASS\n");
	return 0;
}
