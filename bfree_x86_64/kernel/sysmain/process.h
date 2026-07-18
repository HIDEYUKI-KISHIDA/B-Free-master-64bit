#ifndef BFREE_PROCESS_H
#define BFREE_PROCESS_H

#include <stdint.h>
#include "vmm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cooperative children: at most one VFORK/RUNNING (shared single task),
 * but up to BFREE_PROC_MAX_CHILDREN unreaped zombies for wait/pipelines. */

typedef enum {
    BFREE_PROC_FREE = 0,
    BFREE_PROC_VFORK,
    BFREE_PROC_RUNNING,
    BFREE_PROC_ZOMBIE
} bfree_proc_state_t;

#define BFREE_PROC_MAX_CHILDREN 8

typedef struct {
    bfree_proc_state_t state;
    int pid;
    int ppid;
    int exit_status;
    int has_private_as;
    page_table_t *parent_pt;
    page_table_t *child_pt;
} bfree_proc_child_t;
void bfree_process_init(void);

/* Returns 0 and fills *child_pid, or a negative errno. */
long bfree_process_vfork_enter(int *child_pid);

int bfree_process_child_active(void);
int bfree_process_child_pid(void);
int bfree_process_child_has_private_as(void);
page_table_t *bfree_process_parent_pt(void);
page_table_t *bfree_process_child_pt(void);

/* Switch the current task onto a freshly cloned child page table for execve.
 * Allocates/fills child_pt but does not activate CR3 (see activate). */
long bfree_process_exec_commit_as(page_table_t **out_child_pt);

/* Activate child CR3 after ELF and user stack are mapped into child_pt. */
void bfree_process_exec_activate_as(void);

/* Restore parent CR3 / TCB page_table_base and release child AS pages. */
void bfree_process_exit_restore_as(void);

/* Mark zombie with status; does not resume the parent (caller does SYSRET). */
void bfree_process_exit_child(int status);

/* Reap zombie. Returns pid, 0 (WNOHANG), or -errno. */
long bfree_process_wait4(long pid, int *status_out, int options);

#ifdef __cplusplus
}
#endif

#endif /* BFREE_PROCESS_H */
