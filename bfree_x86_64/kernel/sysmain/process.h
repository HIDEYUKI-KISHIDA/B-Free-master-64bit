#ifndef BFREE_PROCESS_H
#define BFREE_PROCESS_H

#include <stdint.h>
#include "vmm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cooperative children on one task:
 * - Up to BFREE_PROC_MAX_LIVE simultaneous VFORK/RUNNING/STOPPED (H02).
 * - At most one VFORK (shared AS) at a time; AS-copy may fill remaining slots.
 * - Up to BFREE_PROC_MAX_CHILDREN unreaped zombies for wait/pipelines.
 */

typedef enum {
    BFREE_PROC_FREE = 0,
    BFREE_PROC_VFORK,
    BFREE_PROC_RUNNING,
    BFREE_PROC_STOPPED, /* H06: job-control stop (SIGTSTP/TTIN/TTOU/STOP) */
    BFREE_PROC_ZOMBIE
} bfree_proc_state_t;

#define BFREE_PROC_MAX_CHILDREN 8
#define BFREE_PROC_MAX_LIVE     4
#define BFREE_WUNTRACED         2
#define BFREE_WCONTINUED        8

typedef struct {
    bfree_proc_state_t state;
    int pid;
    int ppid;
    int pgid;
    int exit_status;   /* exit code (0..255) or signal number if exited_signal */
    int exited_signal; /* 1 → wait status is WIFSIGNALED(exit_status) */
    int stop_sig;      /* signal that stopped us (for WIFSTOPPED status) */
    int stop_pending;  /* 1 until wait(WUNTRACED) reports the stop */
    int has_private_as;
    int fork_pt_idx;   /* AS-copy pool index, or -1 */
    int coop_session;  /* syscall coop session index, or -1 */
    page_table_t *parent_pt;
    page_table_t *child_pt;
} bfree_proc_child_t;

void bfree_process_init(void);

/* Returns 0 and fills *child_pid, or a negative errno. Shared AS (vfork). */
long bfree_process_vfork_enter(int *child_pid);

/* Like vfork_enter but eager-copies the parent user AS. Leaves CR3 on the
 * parent; coop yield installs the child page table when the child runs. */
long bfree_process_fork_enter(int *child_pid);

int bfree_process_live_count(void);
int bfree_process_vfork_count(void);
/* Focus g_active on live pid/slot; 0 ok, -1 not found / not live. */
int bfree_process_select_pid(int pid);
int bfree_process_select_slot(int slot);
int bfree_process_first_live_pid(void);
/* nth live among VFORK/RUNNING in table order; -1 if none. */
int bfree_process_nth_live_pid(int n);
int bfree_process_slot_of_pid(int pid);

int bfree_process_child_active(void);
/* Re-focus g_active on a runnable child before exit_group vfork resume. */
void bfree_process_heal_focus_for_exit(void);
/* Scan live slots (parent_pt or VFORK) so exit_group always finds the session. */
int bfree_process_heal_vfork_exit_session(void);
int bfree_process_child_pid(void);
int bfree_process_child_has_private_as(void);
int bfree_process_child_fork_pt_idx(void);
int bfree_process_child_coop_session(void);
void bfree_process_child_set_coop_session(int sess);
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

/* Mark zombie as killed by signal (wait status WIFSIGNALED). */
void bfree_process_exit_child_signal(int sig);

/* H06: stop/continue cooperative child (keeps AS + session). 0 ok, -errno. */
long bfree_process_stop_pid(int pid, int sig);
long bfree_process_cont_pid(int pid);
int bfree_process_pid_is_stopped(int pid);
void bfree_process_clear_stop_pending(int pid);
/* Runnable live = VFORK/RUNNING (not STOPPED). */
int bfree_process_runnable_count(void);
int bfree_process_first_runnable_pid(void);

int bfree_process_getpgid(int pid);
long bfree_process_setpgid(int pid, int pgid);
/* 1 if any non-FREE child has this pgid (live or zombie). */
int bfree_process_pgid_has_member(int pgid);

/* Reap zombie. Returns pid, 0 (WNOHANG), or -errno.
 * pid==-1: any; pid<-1: process group -pid. */
long bfree_process_wait4(long pid, int *status_out, int options);

/* Heal: turn unschedulable LIVE children into zombies so wait cannot hlt-spin. */
int bfree_process_force_zombie_live(void);
/* Force-zombie all LIVE except keep_pid (keep_pid<=0 → all). */
int bfree_process_force_zombie_except(int keep_pid);

#ifdef __cplusplus
}
#endif

#endif /* BFREE_PROCESS_H */
