#ifndef BFREE_GUEST_THREAD_H
#define BFREE_GUEST_THREAD_H

#define BFREE_GUEST_MAX_THREADS 16
/* Child first resume after clone (rax=0, new RSP). */
#define BFREE_SYSRET_THREAD_CHILD ((long)-4090)
/* Switch to another guest thread (same register restore as fork-parent). */
#define BFREE_SYSRET_THREAD_SWITCH ((long)-4089)

#endif
