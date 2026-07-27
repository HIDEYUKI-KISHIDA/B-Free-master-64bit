#include "sched_abi.h"

#include <errno.h>
#include <string.h>

#define BFREE_SCHED_OTHER 0
#define BFREE_SCHED_FIFO  1
#define BFREE_SCHED_RR    2

static int g_policy = BFREE_SCHED_OTHER;
static int g_priority;
static unsigned long g_affinity = 1; /* CPU0 */
static long g_time_sec;
static long g_time_nsec;
static void *g_rseq;
static unsigned int g_rseq_len;

void bfree_sched_abi_reset(void)
{
	g_policy = BFREE_SCHED_OTHER;
	g_priority = 0;
	g_affinity = 1;
	g_time_sec = 0;
	g_time_nsec = 0;
	g_rseq = NULL;
	g_rseq_len = 0;
}

static int policy_ok(int policy)
{
	return policy == BFREE_SCHED_OTHER || policy == BFREE_SCHED_FIFO ||
	       policy == BFREE_SCHED_RR;
}

int bfree_sched_getparam(int pid, struct bfree_sched_param *param)
{
	(void)pid;
	if (param == NULL)
		return -EFAULT;
	param->sched_priority = g_priority;
	return 0;
}

int bfree_sched_setparam(int pid, const struct bfree_sched_param *param)
{
	(void)pid;
	if (param == NULL)
		return -EFAULT;
	if (param->sched_priority < 0 || param->sched_priority > 99)
		return -EINVAL;
	g_priority = param->sched_priority;
	return 0;
}

int bfree_sched_setscheduler(int pid, int policy,
			     const struct bfree_sched_param *param)
{
	(void)pid;
	if (!policy_ok(policy))
		return -EINVAL;
	if (param == NULL)
		return -EFAULT;
	if (policy == BFREE_SCHED_OTHER) {
		if (param->sched_priority != 0)
			return -EINVAL;
	} else if (param->sched_priority < 1 || param->sched_priority > 99) {
		return -EINVAL;
	}
	g_policy = policy;
	g_priority = param->sched_priority;
	return 0;
}

int bfree_sched_getscheduler(int pid)
{
	(void)pid;
	return g_policy;
}

int bfree_sched_get_priority_max(int policy)
{
	if (policy == BFREE_SCHED_OTHER)
		return 0;
	if (policy == BFREE_SCHED_FIFO || policy == BFREE_SCHED_RR)
		return 99;
	return -EINVAL;
}

int bfree_sched_get_priority_min(int policy)
{
	if (policy == BFREE_SCHED_OTHER)
		return 0;
	if (policy == BFREE_SCHED_FIFO || policy == BFREE_SCHED_RR)
		return 1;
	return -EINVAL;
}

int bfree_sched_rr_get_interval(int pid, void *tp)
{
	long *t = tp;

	(void)pid;
	if (tp == NULL)
		return -EFAULT;
	/* Cooperative guest: report a fixed 10ms quantum. */
	t[0] = 0;
	t[1] = 10000000L;
	return 0;
}

int bfree_sched_getaffinity(int pid, unsigned long cpusetsize, unsigned long *mask)
{
	(void)pid;
	if (mask == NULL || cpusetsize == 0)
		return -EINVAL;
	memset(mask, 0, cpusetsize);
	if (cpusetsize >= sizeof(unsigned long))
		mask[0] = g_affinity;
	else if (cpusetsize >= 1)
		*((unsigned char *)mask) = (unsigned char)(g_affinity & 0xff);
	return 0;
}

int bfree_sched_setaffinity(int pid, unsigned long cpusetsize,
			    const unsigned long *mask)
{
	(void)pid;
	if (mask == NULL || cpusetsize == 0)
		return -EINVAL;
	g_affinity = mask[0] ? mask[0] : 1;
	return 0;
}

int bfree_settimeofday(const void *tv, const void *tz)
{
	const long *t = tv;

	(void)tz;
	if (tv == NULL)
		return -EFAULT;
	/* timeval: tv_sec, tv_usec */
	g_time_sec = t[0];
	g_time_nsec = t[1] * 1000;
	if (g_time_nsec < 0 || g_time_nsec >= 1000000000L) {
		g_time_nsec = 0;
		return -EINVAL;
	}
	return 0;
}

int bfree_clock_settime(int clockid, const void *tp)
{
	const long *t = tp;

	(void)clockid;
	if (tp == NULL)
		return -EFAULT;
	if (t[0] < 0 || t[1] < 0 || t[1] >= 1000000000L)
		return -EINVAL;
	g_time_sec = t[0];
	g_time_nsec = t[1];
	return 0;
}

int bfree_rseq(void *rseq, unsigned int rseq_len, int flags, unsigned int sig)
{
	(void)sig;
	if (flags != 0)
		return -EINVAL;
	if (rseq == NULL)
		return -EFAULT;
	if (rseq_len < 32)
		return -EINVAL;
	g_rseq = rseq;
	g_rseq_len = rseq_len;
	return 0;
}

/* Expose wall time override for clock_gettime/time if set. */
int bfree_sched_abi_time(long *sec, long *nsec)
{
	if (g_time_sec == 0 && g_time_nsec == 0)
		return 0;
	if (sec)
		*sec = g_time_sec;
	if (nsec)
		*nsec = g_time_nsec;
	return 1;
}
