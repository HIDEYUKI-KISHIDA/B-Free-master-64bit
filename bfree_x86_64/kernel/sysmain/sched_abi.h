#ifndef BFREE_SCHED_ABI_H
#define BFREE_SCHED_ABI_H

#include "process.h"

#include <stddef.h>

struct bfree_sched_param {
	int sched_priority;
};

struct bfree_timespec;

int bfree_sched_getparam(int pid, struct bfree_sched_param *param);
int bfree_sched_setparam(int pid, const struct bfree_sched_param *param);
int bfree_sched_setscheduler(int pid, int policy,
			     const struct bfree_sched_param *param);
int bfree_sched_getscheduler(int pid);
int bfree_sched_get_priority_max(int policy);
int bfree_sched_get_priority_min(int policy);
int bfree_sched_rr_get_interval(int pid, void *tp);
int bfree_sched_getaffinity(int pid, unsigned long cpusetsize, unsigned long *mask);
int bfree_sched_setaffinity(int pid, unsigned long cpusetsize,
			    const unsigned long *mask);

int bfree_settimeofday(const void *tv, const void *tz);
int bfree_clock_settime(int clockid, const void *tp);
int bfree_rseq(void *rseq, unsigned int rseq_len, int flags, unsigned int sig);
int bfree_sched_abi_time(long *sec, long *nsec);

void bfree_sched_abi_reset(void);

#endif
