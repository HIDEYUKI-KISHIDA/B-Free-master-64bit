#include "thread.h"

#include <errno.h>
#include <string.h>

#define BFREE_MAX_FUTEX 16

struct bfree_futex_waiter {
	int in_use;
	int pid;
	int *uaddr;
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

	(void)stack;
	(void)tls;
	if (mgr == NULL || fn == NULL)
		return -EINVAL;
	if (!(flags & BFREE_CLONE_VM) || !(flags & BFREE_CLONE_THREAD))
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

	if (flags & BFREE_CLONE_VM) {
		child->as.mem = parent->as.mem;
		child->as.size = parent->as.size;
		child->as.brk_end = parent->as.brk_end;
		child->as.mmap_next = parent->as.mmap_next;
		child->as_shared = 1;
	} else {
		int rc;

		rc = bfree_as_fork_copy(&child->as, &parent->as);
		if (rc < 0) {
			child->state = BFREE_PROC_FREE;
			return rc;
		}
	}

	if (parent_tid != NULL)
		*parent_tid = parent->pid;
	if (child_tid != NULL)
		*child_tid = child->pid;

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
			if (child->thread_fn == NULL)
				return -EINVAL;
			rc = child->thread_fn(child->thread_arg);
			child->exit_status = rc & 0xff;
			child->state = BFREE_PROC_ZOMBIE;
			mgr->current = parent_idx;
			return 0;
		}
	}
	return -ESRCH;
}

int bfree_futex(int *uaddr, int op, int val, const void *timeout)
{
	int i;
	int woke = 0;

	(void)timeout;
	if (uaddr == NULL)
		return -EINVAL;

	op &= ~BFREE_FUTEX_PRIVATE_FLAG;

	if (op == BFREE_FUTEX_WAIT) {
		for (i = 0; i < BFREE_MAX_FUTEX; i++) {
			if (!futex_waiters[i].in_use) {
				futex_waiters[i].in_use = 1;
				futex_waiters[i].uaddr = uaddr;
				futex_waiters[i].pid = 0;
				return 0;
			}
		}
		return -EAGAIN;
	}

	if (op == BFREE_FUTEX_WAKE) {
		for (i = 0; i < BFREE_MAX_FUTEX && woke < val; i++) {
			if (futex_waiters[i].in_use &&
			    futex_waiters[i].uaddr == uaddr) {
				futex_waiters[i].in_use = 0;
				woke++;
			}
		}
		return woke;
	}

	if (op == (BFREE_FUTEX_WAIT | BFREE_FUTEX_OWNER_DIED)) {
		*uaddr = 0;
		return 0;
	}

	return -ENOSYS;
}
