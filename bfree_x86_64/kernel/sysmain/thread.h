/*
 * Cooperative threads (clone) and futex (M4).
 */
#ifndef BFREE_THREAD_H
#define BFREE_THREAD_H

#include "process.h"

#include <stdint.h>

#define BFREE_CLONE_VM            0x00000100
#define BFREE_CLONE_THREAD        0x00010000
#define BFREE_CLONE_PARENT_SETTID 0x00100000

#define BFREE_FUTEX_WAIT          0
#define BFREE_FUTEX_WAKE          1
#define BFREE_FUTEX_PRIVATE_FLAG  128
#define BFREE_FUTEX_OWNER_DIED    0x40000000

int bfree_clone(struct bfree_proc_mgr *mgr, unsigned long flags,
		void *stack, int *parent_tid, void *tls, int *child_tid,
		bfree_thread_fn fn, void *arg);
int bfree_thread_run(struct bfree_proc_mgr *mgr, int tid);
int bfree_futex(int *uaddr, int op, int val, const void *timeout);
int bfree_futex_on(struct bfree_proc_mgr *mgr, int *uaddr, int op, int val,
		   const void *timeout);

#endif /* BFREE_THREAD_H */
