/*
 * Guest syscall API (M1–M6).
 */
#ifndef BFREE_SYSCALL_H
#define BFREE_SYSCALL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

void guest_init(void);
struct bfree_fs *guest_fs(void);
struct bfree_proc_mgr *guest_proc_mgr(void);

long bfree_invoke_syscall(unsigned long nr, unsigned long a0, unsigned long a1,
			  unsigned long a2, unsigned long a3, unsigned long a4,
			  unsigned long a5);

/* x86_64 Linux syscall register convention entry (M6 trap stub). */
long bfree_trap_syscall_entry(unsigned long nr, unsigned long a0,
			      unsigned long a1, unsigned long a2,
			      unsigned long a3, unsigned long a4);

#endif
