/*
 * P18_SECOND_MAP_24 — fill BusyBox second-map batch (9 ENOSYS + 15 THIN).
 */
#include "sched_abi.h"
#include "syscall.h"
#include "syscall_dispatch.h"
#include "thread.h"
#include "fs_ofd.h"

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
	struct bfree_sched_param sp;
	unsigned long mask = 0;
	int futex_word = 1;
	char termios_buf[64];
	long tv[2];
	long ts[2];
	char rseq_buf[64];
	const int need[] = {143, 144, 145, 146, 147, 164, 204, 227, 334};
	unsigned i;

	guest_init();

	for (i = 0; i < sizeof(need) / sizeof(need[0]); i++)
		CHECK(bfree_syscall_is_implemented((unsigned long)need[i]),
		      "second-map ENOSYS nr registered");

	/* A: former ENOSYS */
	memset(&sp, 0, sizeof(sp));
	CHECK(bfree_invoke_syscall(146, 0, 0, 0, 0, 0, 0) == 0, "prio max OTHER");
	CHECK(bfree_invoke_syscall(147, 0, 0, 0, 0, 0, 0) == 0, "prio min OTHER");
	CHECK(bfree_invoke_syscall(146, 1, 0, 0, 0, 0, 0) == 99, "prio max FIFO");
	sp.sched_priority = 10;
	CHECK(bfree_invoke_syscall(144, 0, 1, (unsigned long)&sp, 0, 0, 0) == 0,
	      "setscheduler");
	CHECK(bfree_invoke_syscall(145, 0, 0, 0, 0, 0, 0) == 1, "getscheduler");
	CHECK(bfree_invoke_syscall(143, 0, (unsigned long)&sp, 0, 0, 0, 0) == 0,
	      "getparam");
	CHECK(sp.sched_priority == 10, "priority saved");
	CHECK(bfree_invoke_syscall(204, 0, sizeof(mask), (unsigned long)&mask, 0,
				   0, 0) == 0,
	      "getaffinity");
	CHECK(mask != 0, "affinity mask");

	tv[0] = 1000;
	tv[1] = 0;
	CHECK(bfree_invoke_syscall(164, (unsigned long)tv, 0, 0, 0, 0, 0) == 0,
	      "settimeofday");
	ts[0] = 2000;
	ts[1] = 123;
	CHECK(bfree_invoke_syscall(227, 0, (unsigned long)ts, 0, 0, 0, 0) == 0,
	      "clock_settime");
	memset(rseq_buf, 0, sizeof(rseq_buf));
	CHECK(bfree_invoke_syscall(334, (unsigned long)rseq_buf, 32, 0, 0, 0, 0) ==
		      0,
	      "rseq");
	CHECK(bfree_invoke_syscall(439, BFREE_AT_FDCWD,
				   (unsigned long)"/tmp", 0, 0, 0, 0) == 0,
	      "faccessat2");
	CHECK(bfree_syscall_is_implemented(142), "sched_setparam pair");
	CHECK(bfree_syscall_is_implemented(274), "get_robust_list pair");
	CHECK(bfree_syscall_is_implemented(286), "timerfd_settime pair");

	/* B: THIN thickened */
	CHECK(bfree_invoke_syscall(13, 2, 0, 0, 8, 0, 0) == 0, "rt_sigaction");
	CHECK(bfree_invoke_syscall(14, 2, 0, 0, 8, 0, 0) == 0, "rt_sigprocmask");
	memset(termios_buf, 0, sizeof(termios_buf));
	CHECK(bfree_invoke_syscall(16, 0, 0x5401, (unsigned long)termios_buf, 0,
				   0, 0) == 0,
	      "ioctl TCGETS");
	CHECK(bfree_invoke_syscall(16, 0, 0x5413, (unsigned long)termios_buf, 0,
				   0, 0) == 0,
	      "ioctl TIOCGWINSZ");

	futex_word = 1;
	rc = bfree_futex_on(guest_proc_mgr(), &futex_word, 0, 1, NULL);
	CHECK(rc == 0, "futex wait cooperative");
	futex_word = 7;
	CHECK(bfree_futex_on(guest_proc_mgr(), &futex_word, 1, 1, NULL) >= 0,
	      "futex wake");

	CHECK(bfree_syscall_enosys_count() == 187, "enosys after incomplete-pair hygiene");

	printf("P18_SECOND_MAP_24: PASS\n");
	return 0;
}
