/*
 * Ring-3 cooperative scheduler for QEMU guest (M16).
 */
#ifndef BFREE_RING3_SCHED_H
#define BFREE_RING3_SCHED_H

#include "process.h"

#include <stdint.h>

void bfree_ring3_proc_init(struct bfree_proc *proc);
void bfree_ring3_pre_syscall(struct bfree_proc_mgr *mgr, uint64_t rsp,
			     uint64_t rcx, uint64_t r11);
uint64_t bfree_ring3_post_syscall(struct bfree_proc_mgr *mgr, uint64_t rax);
void bfree_ring3_on_fork(struct bfree_proc_mgr *mgr, int child_idx,
			 int child_pid);
void bfree_ring3_on_vfork(struct bfree_proc_mgr *mgr);
void bfree_ring3_on_execve_parent(struct bfree_proc_mgr *mgr);
void bfree_ring3_block(struct bfree_proc_mgr *mgr, bfree_proc_state_t state);
void bfree_ring3_wake_waiters(struct bfree_proc_mgr *mgr);
void bfree_ring3_wake_pipe_readers(struct bfree_proc_mgr *mgr, int pipe_idx);
void bfree_ring3_after_exit(struct bfree_proc_mgr *mgr);
void bfree_ring3_dispatch_user(struct bfree_proc_mgr *mgr);
void bfree_ring3_syscall_pre(uint64_t rsp, uint64_t rcx, uint64_t r11);
void bfree_ring3_syscall_post(uint64_t rax);

extern uint64_t ring3_sysret_rax;

#endif /* BFREE_RING3_SCHED_H */
