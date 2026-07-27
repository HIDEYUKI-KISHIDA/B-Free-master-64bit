#include "thread.h"
#include "fs_ofd.h"

#include <errno.h>
#include <string.h>

#define BFREE_MAX_FUTEX 16
#define BFREE_FUTEX_WAIT_SPINS 64

struct bfree_futex_waiter {
	int in_use;
	int pid;
	int *uaddr;
	int val;
};

static struct bfree_futex_waiter futex_waiters[BFREE_MAX_FUTEX];

static int alloc_slot(struct bfree_proc_mgr *mgr)
{
	int i;

	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state == BFREE_PROC_FREE)
			return i;
	}
	return -1;
}

int bfree_clone(struct bfree_proc_mgr *mgr, unsigned long flags,
		void *stack, int *parent_tid, void *tls, int *child_tid,
		bfree_thread_fn fn, void *arg)
{
	struct bfree_proc *parent;
	struct bfree_proc *child;
	int child_idx;

	(void)tls;
	if (mgr == NULL)
		return -EINVAL;

	/* Non-thread clone maps to fork (Linux ABI). */
	if (!(flags & BFREE_CLONE_THREAD))
		return bfree_fork(mgr);

	if (!(flags & BFREE_CLONE_VM))
		return -ENOSYS;

	parent = bfree_proc_current(mgr);
	if (parent == NULL)
		return -ESRCH;

	child_idx = alloc_slot(mgr);
	if (child_idx < 0)
		return -EAGAIN;

	child = &mgr->procs[child_idx];
	memset(child, 0, sizeof(*child));
	child->pid = mgr->next_pid++;
	child->ppid = parent->pid;
	child->state = BFREE_PROC_RUNNABLE;
	child->is_thread = 1;
	child->pgid = parent->pgid;
	child->sid = parent->sid;
	child->sig_mask = parent->sig_mask;
	memcpy(child->sig_handler, parent->sig_handler, sizeof(child->sig_handler));
	memcpy(child->sig_restorer, parent->sig_restorer, sizeof(child->sig_restorer));
	memcpy(child->sig_sa_mask, parent->sig_sa_mask, sizeof(child->sig_sa_mask));
	bfree_fs_init_proc_fds(child->fd_ofd, child->fd_flags);
	/* Inherit parent's open files (same path as bfree_fork). */
	{
		struct bfree_fs *fs = bfree_proc_exec_fs();

		if (fs != NULL)
			bfree_fs_fork_fds(fs, child->fd_ofd, child->fd_flags,
					  parent->fd_ofd, parent->fd_flags);
	}

	child->as = parent->as;
	child->as_shared = 1;

	if ((flags & BFREE_CLONE_PARENT_SETTID) && parent_tid != NULL)
		*parent_tid = child->pid;
	else if (parent_tid != NULL)
		*parent_tid = parent->pid;

	if (child_tid != NULL) {
		*child_tid = child->pid;
		child->clear_tid_addr_set = 1;
		child->clear_child_tid = child_tid;
	}

	/* stack is recorded for ring-3 path; host harness uses fn. */
	(void)stack;
	child->thread_fn = fn;
	child->thread_arg = arg;
	return child->pid;
}

int bfree_thread_run(struct bfree_proc_mgr *mgr, int tid)
{
	struct bfree_proc *child;
	int parent_idx;
	int i;
	int rc;

	parent_idx = mgr->current;
	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state != BFREE_PROC_FREE &&
		    mgr->procs[i].pid == tid) {
			child = &mgr->procs[i];
			mgr->current = i;
			if (child->thread_fn == NULL) {
				/* Ring-3 style thread: just leave runnable. */
				mgr->current = parent_idx;
				return 0;
			}
			rc = child->thread_fn(child->thread_arg);
			child->exit_status = rc & 0xff;
			child->state = BFREE_PROC_ZOMBIE;
			if (child->clear_tid_addr_set &&
			    child->clear_child_tid != NULL)
				*child->clear_child_tid = 0;
			mgr->current = parent_idx;
			return 0;
		}
	}
	return -ESRCH;
}

int bfree_futex(int *uaddr, int op, int val, const void *timeout)
{
	return bfree_futex_on(NULL, uaddr, op, val, timeout);
}

int bfree_futex_on(struct bfree_proc_mgr *mgr, int *uaddr, int op, int val,
		   const void *timeout)
{
	int i;
	int woke = 0;
	int spins;

	(void)timeout;
	if (uaddr == NULL)
		return -EINVAL;

	op &= ~BFREE_FUTEX_PRIVATE_FLAG;

	if (op == BFREE_FUTEX_WAIT) {
		if (*uaddr != val)
			return -EAGAIN;
		for (i = 0; i < BFREE_MAX_FUTEX; i++) {
			if (!futex_waiters[i].in_use) {
				futex_waiters[i].in_use = 1;
				futex_waiters[i].uaddr = uaddr;
				futex_waiters[i].val = val;
				futex_waiters[i].pid =
					mgr && bfree_proc_current(mgr)
						? bfree_proc_current(mgr)->pid
						: 0;
				break;
			}
		}
		if (i >= BFREE_MAX_FUTEX)
			return -EAGAIN;

		/* Cooperative wait: yield until woken or value changes. */
		for (spins = 0; spins < BFREE_FUTEX_WAIT_SPINS; spins++) {
			if (!futex_waiters[i].in_use)
				return 0;
			if (*uaddr != val) {
				futex_waiters[i].in_use = 0;
				return -EAGAIN;
			}
			if (mgr != NULL)
				bfree_sched_tick(mgr);
		}
		futex_waiters[i].in_use = 0;
		return 0;
	}

	if (op == BFREE_FUTEX_WAKE) {
		for (i = 0; i < BFREE_MAX_FUTEX && woke < val; i++) {
			if (futex_waiters[i].in_use &&
			    futex_waiters[i].uaddr == uaddr) {
				futex_waiters[i].in_use = 0;
				woke++;
			}
		}
		if (mgr != NULL && woke > 0)
			bfree_sched_tick(mgr);
		return woke;
	}

	if (op == (BFREE_FUTEX_WAIT | BFREE_FUTEX_OWNER_DIED)) {
		*uaddr = 0;
		return 0;
	}

	return -ENOSYS;
}
