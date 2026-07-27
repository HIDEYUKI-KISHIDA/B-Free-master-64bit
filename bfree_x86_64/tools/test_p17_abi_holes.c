/*
 * P17_ABI_HOLES — categories 0–4 smoke coverage.
 */
#include "fs_ofd.h"
#include "net_unix.h"
#include "process.h"
#include "syscall.h"
#include "syscall_dispatch.h"

#include <errno.h>
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
	long rc;
	char uname_buf[512];
	char tbuf[64];
	struct bfree_linux_stat st;
	int enosys;
	struct bfree_fs *fs;
	struct bfree_proc_mgr *mgr;
	int fd;
	int child;

	guest_init();
	fs = guest_fs();
	mgr = guest_proc_mgr();

	/* Cat0: chmod/fchmod/prctl/cap/prlimit64 correct NRs */
	CHECK(bfree_syscall_is_implemented(90), "chmod registered");
	CHECK(bfree_syscall_is_implemented(91), "fchmod registered");
	CHECK(bfree_syscall_is_implemented(125), "capget registered");
	CHECK(bfree_syscall_is_implemented(126), "capset registered");
	CHECK(bfree_syscall_is_implemented(157), "prctl registered");
	CHECK(bfree_syscall_is_implemented(302), "prlimit64 registered");

	CHECK(bfree_create(fs, "/tmp/p17", 0644) == 0, "create");
	rc = bfree_invoke_syscall(90, (unsigned long)"/tmp/p17", 0600, 0, 0, 0, 0);
	CHECK(rc == 0, "chmod nr90");
	CHECK(bfree_stat(fs, "/tmp/p17", &st) == 0, "stat after chmod");
	CHECK((st.st_mode & 0777) == 0600, "mode 0600");

	rc = bfree_invoke_syscall(125, 0, 0, 0, 0, 0, 0);
	CHECK(rc == 0, "capget nr125");
	rc = bfree_invoke_syscall(157, 1, 0, 0, 0, 0, 0); /* PR_SET_PDEATHSIG-ish */
	CHECK(rc == 0 || rc == -EINVAL, "prctl nr157");
	rc = bfree_invoke_syscall(302, 0, 7, 0, 0, 0, 0); /* RLIMIT_NOFILE get */
	CHECK(rc == 0, "prlimit64 nr302");

	/* Cat1: mkdir/rename/uname/clock/access */
	rc = bfree_invoke_syscall(83, (unsigned long)"/tmp/p17dir", 0755, 0, 0, 0, 0);
	CHECK(rc == 0, "mkdir");
	rc = bfree_invoke_syscall(82, (unsigned long)"/tmp/p17dir",
				  (unsigned long)"/tmp/p17dir2", 0, 0, 0, 0);
	CHECK(rc == 0, "rename");
	rc = bfree_invoke_syscall(21, (unsigned long)"/tmp/p17", 0, 0, 0, 0, 0);
	CHECK(rc == 0, "access");
	memset(uname_buf, 0, sizeof(uname_buf));
	rc = bfree_invoke_syscall(63, (unsigned long)uname_buf, 0, 0, 0, 0, 0);
	CHECK(rc == 0, "uname");
	CHECK(memcmp(uname_buf, "B-Free", 6) == 0, "uname sysname");
	rc = bfree_invoke_syscall(228, 0, (unsigned long)tbuf, 0, 0, 0, 0);
	CHECK(rc == 0, "clock_gettime");

	/* Cat2: listen/accept path + sigaction */
	rc = bfree_invoke_syscall(13, 2, 0, 0, 8, 0, 0);
	CHECK(rc == 0, "rt_sigaction");
	{
		int srv, cli, acc;
		struct bfree_sockaddr_un addr;

		memset(&addr, 0, sizeof(addr));
		addr.sun_family = 1;
		memcpy(addr.sun_path, "/tmp/p17.sock", 14);
		srv = (int)bfree_invoke_syscall(41, 1, 1, 0, 0, 0, 0);
		CHECK(srv >= 0, "socket");
		CHECK(bfree_invoke_syscall(49, (unsigned long)srv,
					   (unsigned long)&addr, 110, 0, 0, 0) == 0,
		      "bind");
		CHECK(bfree_invoke_syscall(50, (unsigned long)srv, 1, 0, 0, 0, 0) == 0,
		      "listen");
		cli = (int)bfree_invoke_syscall(41, 1, 1, 0, 0, 0, 0);
		CHECK(cli >= 0, "client socket");
		CHECK(bfree_invoke_syscall(42, (unsigned long)cli,
					   (unsigned long)&addr, 110, 0, 0, 0) == 0,
		      "connect");
		acc = (int)bfree_invoke_syscall(43, (unsigned long)srv, 0, 0, 0, 0, 0);
		CHECK(acc >= 0, "accept");
	}

	/* Cat3: sem/msg/epoll */
	rc = bfree_invoke_syscall(64, 0x11, 1, 01000, 0, 0, 0);
	CHECK(rc >= 0, "semget");
	rc = bfree_invoke_syscall(68, 0x22, 01000, 0, 0, 0, 0);
	CHECK(rc >= 0, "msgget");
	rc = bfree_invoke_syscall(291, 0, 0, 0, 0, 0, 0);
	CHECK(rc >= 0, "epoll_create1");

	/* Cat4: per-proc FD tables — child does not see parent-only open after
	 * separate bind; fork should duplicate OFD refs. */
	fd = bfree_open(fs, "/tmp/p17", 0, 0);
	CHECK(fd >= 0, "open parent fd");
	child = bfree_fork(mgr);
	CHECK(child > 0, "fork");
	CHECK(mgr->procs[0].fd_ofd[fd] >= 0, "parent still has fd");
	/* Find child slot */
	{
		int i, found = 0;
		for (i = 0; i < BFREE_MAX_PROC; i++) {
			if (mgr->procs[i].pid == child) {
				CHECK(mgr->procs[i].fd_ofd[fd] >= 0,
				      "child inherited fd");
				found = 1;
				break;
			}
		}
		CHECK(found, "child slot");
	}
	bfree_sched_tick(mgr);
	CHECK(bfree_sched_ticks(mgr) > 0, "sched tick");

	enosys = bfree_syscall_enosys_count();
	CHECK(enosys > 0 && enosys < 336, "enosys reduced below M16");
	CHECK(enosys == 210, "enosys exactly 210 in 0..399");

	printf("P17_ABI_HOLES: PASS (enosys=%d)\n", enosys);
	return 0;
}
