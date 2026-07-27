/*
 * Ring-3 cooperative scheduler for QEMU guest (M16).
 */
#include "ring3_sched.h"

#include "syscall.h"

#include <stddef.h>

extern void bfree_ring3_do_sysret(uint64_t rax, uint64_t rcx, uint64_t r11,
				  uint64_t rsp);

uint64_t ring3_sysret_rcx;
uint64_t ring3_sysret_r11;
uint64_t ring3_sysret_rsp;
uint64_t ring3_sysret_rax;

static struct bfree_proc *proc_at(struct bfree_proc_mgr *mgr, int idx)
{
	if (idx < 0 || idx >= BFREE_MAX_PROC)
		return NULL;
	if (mgr->procs[idx].state == BFREE_PROC_FREE)
		return NULL;
	return &mgr->procs[idx];
}

static struct bfree_proc *current_proc(struct bfree_proc_mgr *mgr)
{
	return proc_at(mgr, mgr->current);
}

static int is_blocked(struct bfree_proc *proc)
{
	if (proc == NULL)
		return 0;
	return proc->state == BFREE_PROC_BLOCKED_VFORK ||
	       proc->state == BFREE_PROC_BLOCKED_WAIT ||
	       proc->state == BFREE_PROC_BLOCKED_IO;
}

static int pick_user_runnable(struct bfree_proc_mgr *mgr)
{
	int i;
	int start;

	start = mgr->current;
	for (i = 0; i < BFREE_MAX_PROC; i++) {
		int idx = (start + 1 + i) % BFREE_MAX_PROC;
		struct bfree_proc *p = &mgr->procs[idx];

		if (p->state == BFREE_PROC_RUNNABLE)
			return idx;
	}
	for (i = 0; i < BFREE_MAX_PROC; i++) {
		struct bfree_proc *p = &mgr->procs[i];

		if (p->state == BFREE_PROC_RUNNING && !is_blocked(p))
			return i;
	}
	return -1;
}

static void run_user_proc(struct bfree_proc_mgr *mgr, int idx)
{
	struct bfree_proc *proc = &mgr->procs[idx];
	uint64_t rax;

	mgr->current = idx;
	proc->state = BFREE_PROC_RUNNING;
	if (!proc->ring3.valid)
		return;

	rax = proc->ring3.rax;
	if (proc->ring3.fork_child) {
		rax = 0;
		proc->ring3.fork_child = 0;
	}
	if (proc->state == BFREE_PROC_BLOCKED_VFORK)
		rax = 0;
	if (proc->vfork_done > 0) {
		rax = (uint64_t)(unsigned long)proc->vfork_done;
		proc->vfork_done = 0;
	}

	ring3_sysret_rcx = proc->ring3.rcx;
	ring3_sysret_r11 = proc->ring3.r11;
	ring3_sysret_rsp = proc->ring3.rsp;
	bfree_ring3_do_sysret(rax, proc->ring3.rcx, proc->ring3.r11,
			      proc->ring3.rsp);
}

void bfree_ring3_dispatch_user(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *blocked;
	int idx;

	if (mgr == NULL)
		return;
	blocked = current_proc(mgr);
	for (;;) {
		if (blocked == NULL || !is_blocked(blocked))
			return;
		idx = pick_user_runnable(mgr);
		if (idx < 0) {
			__asm__ volatile("pause");
			continue;
		}
		run_user_proc(mgr, idx);
	}
}

void bfree_ring3_proc_init(struct bfree_proc *proc)
{
	if (proc == NULL)
		return;
	proc->ring3.valid = 0;
	proc->ring3.fork_child = 0;
}

void bfree_ring3_pre_syscall(struct bfree_proc_mgr *mgr, uint64_t rsp,
			     uint64_t rcx, uint64_t r11)
{
	struct bfree_proc *self;

	if (mgr == NULL)
		return;
	self = current_proc(mgr);
	if (self == NULL)
		return;
	self->ring3.rsp = rsp;
	self->ring3.rcx = rcx;
	self->ring3.r11 = r11;
	self->ring3.valid = 1;
	if (self->state == BFREE_PROC_RUNNABLE)
		self->state = BFREE_PROC_RUNNING;
}

uint64_t bfree_ring3_post_syscall(struct bfree_proc_mgr *mgr, uint64_t rax)
{
	struct bfree_proc *self;

	if (mgr == NULL)
		return rax;
	self = current_proc(mgr);
	if (self == NULL)
		return rax;
	if (self->ring3.valid) {
		self->ring3.rax = rax;
		ring3_sysret_rcx = self->ring3.rcx;
		ring3_sysret_r11 = self->ring3.r11;
		ring3_sysret_rsp = self->ring3.rsp;
	}
	if (self->state == BFREE_PROC_ZOMBIE)
		bfree_ring3_dispatch_user(mgr);
	if (self->ring3.fork_child)
		rax = 0;
	if (self->vfork_done > 0) {
		rax = (uint64_t)(unsigned long)self->vfork_done;
		self->vfork_done = 0;
	}
	return rax;
}

void bfree_ring3_on_fork(struct bfree_proc_mgr *mgr, int child_idx,
			 int child_pid)
{
	struct bfree_proc *parent;
	struct bfree_proc *child;

	parent = current_proc(mgr);
	child = proc_at(mgr, child_idx);
	if (parent == NULL || child == NULL)
		return;
	child->ring3 = parent->ring3;
	child->ring3.fork_child = 1;
	child->ring3.rax = 0;
	parent->ring3.rax = (uint64_t)(unsigned long)child_pid;
	(void)child_pid;
}

void bfree_ring3_on_vfork(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *child;

	child = current_proc(mgr);
	if (child == NULL)
		return;
	child->ring3.fork_child = 0;
	child->ring3.rax = 0;
}

void bfree_ring3_on_execve_parent(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *parent;
	int i;

	for (i = 0; i < BFREE_MAX_PROC; i++) {
		parent = &mgr->procs[i];
		if (parent->state == BFREE_PROC_BLOCKED_VFORK &&
		    parent->pid == current_proc(mgr)->ppid) {
			parent->state = BFREE_PROC_RUNNING;
			parent->vfork_done = current_proc(mgr)->pid;
			mgr->current = i;
			return;
		}
	}
}

void bfree_ring3_block(struct bfree_proc_mgr *mgr, bfree_proc_state_t state)
{
	struct bfree_proc *self;

	if (mgr == NULL)
		return;
	self = current_proc(mgr);
	if (self == NULL)
		return;
	self->state = state;
	bfree_ring3_dispatch_user(mgr);
}

void bfree_ring3_wake_waiters(struct bfree_proc_mgr *mgr)
{
	int i;

	if (mgr == NULL)
		return;
	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state == BFREE_PROC_BLOCKED_WAIT)
			mgr->procs[i].state = BFREE_PROC_RUNNING;
	}
}

void bfree_ring3_wake_pipe_readers(struct bfree_proc_mgr *mgr, int pipe_idx)
{
	int i;

	(void)pipe_idx;
	if (mgr == NULL)
		return;
	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state == BFREE_PROC_BLOCKED_IO)
			mgr->procs[i].state = BFREE_PROC_RUNNING;
	}
}

void bfree_ring3_after_exit(struct bfree_proc_mgr *mgr)
{
	bfree_ring3_wake_waiters(mgr);
	bfree_ring3_dispatch_user(mgr);
}

static void maybe_schedule_other(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *self;
	int idx;

	self = current_proc(mgr);
	if (self == NULL || is_blocked(self))
		return;
	idx = pick_user_runnable(mgr);
	if (idx < 0 || idx == mgr->current)
		return;
	if (self->state == BFREE_PROC_RUNNING)
		self->state = BFREE_PROC_RUNNABLE;
	run_user_proc(mgr, idx);
}

void bfree_ring3_syscall_pre(uint64_t rsp, uint64_t rcx, uint64_t r11)
{
	bfree_ring3_pre_syscall(guest_proc_mgr(), rsp, rcx, r11);
}

void bfree_ring3_syscall_post(uint64_t rax)
{
	struct bfree_proc_mgr *mgr = guest_proc_mgr();

	ring3_sysret_rax = bfree_ring3_post_syscall(mgr, rax);
	maybe_schedule_other(mgr);
}
