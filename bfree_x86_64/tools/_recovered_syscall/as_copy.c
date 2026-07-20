/* Recovered AS-copy / parked-heap / CR3 switch / coop yield hooks.
 * Sources: 97982f27, 0ce9e498, cf433436 agent transcripts (StrReplace payloads).
 * Apply AFTER tools/_patch_unix_coop.py (needs coop FD snap) and stage1 fork parent-first.
 */

/* --- globals (merge carefully; some fork_* already exist in HEAD) --- */

/* 1 if this coop child was created via SYS_fork AS-copy (parent kept running).
 * Distinct from has_private_as: vfork+exec also gains a private AS, but the
 * parent was frozen and still needs the shared-AS stack snapshot restored. */
static int g_guest_fork_was_as_copy;

/* AS-copy: kernel brk/cwd are global. Dual-park across coop switches so each
 * side resumes its own markers. On child exit restore parent park (not
 * fork-enter) so post-fork parent brk is not rewound. First child resume uses
 * fork-enter (birth heap) until the child gets its own park slot. */
static int g_guest_parent_parked_heap_valid;
static uint64_t g_guest_parent_parked_heap_next;
static uint64_t g_guest_parent_parked_brk;
static char g_guest_parent_parked_cwd[256];
static int g_guest_child_parked_heap_valid;
static uint64_t g_guest_child_parked_heap_next;
static uint64_t g_guest_child_parked_brk;
static char g_guest_child_parked_cwd[256];


static void bfree_guest_cwd_copy(char *dst, size_t dst_sz, const char *src)
{
    size_t i;

    for (i = 0; i < dst_sz; ++i) {
        dst[i] = src[i];
        if (src[i] == '\0') {
            break;
        }
    }
    if (dst_sz > 0U) {
        dst[dst_sz - 1U] = '\0';
    }
}

static void bfree_guest_load_heap_cwd(const char *cwd_src, uint64_t heap_src, uint64_t brk_src)
{
    bfree_guest_cwd_copy(g_guest_cwd, sizeof(g_guest_cwd), cwd_src);
    g_guest_heap_next = heap_src;
    g_guest_brk = brk_src;
}

/* Park live kernel heap/cwd into the parent or child AS-copy slot. */
static void bfree_guest_park_heap_side(int child_side)
{
    if (child_side) {
        g_guest_child_parked_heap_next = g_guest_heap_next;
        g_guest_child_parked_brk = g_guest_brk;
        bfree_guest_cwd_copy(g_guest_child_parked_cwd, sizeof(g_guest_child_parked_cwd),
                             g_guest_cwd);
        g_guest_child_parked_heap_valid = 1;
    } else {
        g_guest_parent_parked_heap_next = g_guest_heap_next;
        g_guest_parent_parked_brk = g_guest_brk;
        bfree_guest_cwd_copy(g_guest_parent_parked_cwd, sizeof(g_guest_parent_parked_cwd),
                             g_guest_cwd);
        g_guest_parent_parked_heap_valid = 1;
    }
}

/* Parent → child: park parent; load child park or birth (fork-enter) heap. */
static void bfree_guest_as_copy_switch_heap_to_child(void)
{
    bfree_guest_park_heap_side(0);
    if (g_guest_child_parked_heap_valid) {
        bfree_guest_load_heap_cwd(g_guest_child_parked_cwd,
                                  g_guest_child_parked_heap_next,
                                  g_guest_child_parked_brk);
    } else {
        bfree_guest_load_heap_cwd(g_guest_fork_saved_cwd,
                                  g_guest_fork_saved_heap_next,
                                  g_guest_fork_saved_brk);
    }
}

/* Child → parent: park child; load parent park. */
static void bfree_guest_as_copy_switch_heap_to_parent(void)
{
    bfree_guest_park_heap_side(1);
    if (g_guest_parent_parked_heap_valid) {
        bfree_guest_load_heap_cwd(g_guest_parent_parked_cwd,
                                  g_guest_parent_parked_heap_next,
                                  g_guest_parent_parked_brk);
    }
}

static void bfree_guest_restore_parent_isol(int as_copy)
{
    if (as_copy && g_guest_parent_parked_heap_valid) {
        bfree_guest_load_heap_cwd(g_guest_parent_parked_cwd,
                                  g_guest_parent_parked_heap_next,
                                  g_guest_parent_parked_brk);
    } else {
        bfree_guest_load_heap_cwd(g_guest_fork_saved_cwd,
                                  g_guest_fork_saved_heap_next,
                                  g_guest_fork_saved_brk);
    }
    g_guest_parent_parked_heap_valid = 0;
    g_guest_child_parked_heap_valid = 0;
}

/* H02: AS-copy coop must flip CR3 with the logical parent/child side. */
static void bfree_coop_as_switch_to(int side)
{
    page_table_t *pt;

    if (!knl_current_task) {
        return;
    }
    if (!bfree_process_child_has_private_as()) {
        return; /* vfork / shared AS — nothing to flip */
    }
    pt = (side == 0) ? bfree_process_parent_pt() : bfree_process_child_pt();
    if (!pt) {
        return;
    }
    if ((page_table_t *)knl_current_task->page_table_base == pt) {
        return;
    }
    knl_current_task->page_table_base = pt;
    __asm__ volatile("mov %0, %%cr3" :: "r"(pt) : "memory");
}


static long bfree_coop_yield_to_parent(void)
{
    bfree_coop_save_child_user();
    if (g_coop_child_rcx >= 2) {
        g_coop_child_rcx -= 2;
    }
    /* Park child for re-exec of the in-flight Linux NR (write=1, wait=61, …). */
    g_coop_child_resume_rax = (uint64_t)g_coop_cur_nr;
    g_coop_child_resume_mode = 1;
    g_coop_child_blocked = 1;
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_parent();
    }
    bfree_coop_fd_switch_to(0);
    bfree_coop_as_switch_to(0);
    /* Parent TLS — child execve may have cleared FS_BASE. */
    if (g_guest_fork_saved_fsbase != 0) {
        if (knl_current_task != 0) {
            knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
    }
    bfree_coop_publish_parent_callee_saved();
    g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
    g_bfree_sysret_exec_rcx = g_bfree_fork_saved_rcx;
    g_bfree_sysret_exec_r11 = g_bfree_fork_saved_r11;
    bfree_coop_arm_parent_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

static long bfree_coop_yield_to_child(void)
{
    g_bfree_fork_saved_rcx = g_bfree_user_sysret_rcx;
    g_bfree_fork_saved_r11 = g_bfree_user_sysret_r11;
    g_bfree_fork_saved_rsp = g_bfree_user_sysret_rsp;
    /* Park parent callee-saved before fork_saved_* is loaded with the child. */
    bfree_coop_save_parent_callee_saved();
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_child();
    }
    g_coop_parent_resume_rax = (uint64_t)g_coop_cur_nr;
    g_coop_parent_resume_mode = 1;
    bfree_coop_fd_switch_to(1);
    /* Child inherits parent's TLS base (copied AS); re-arm before CR3 flip. */
    if (g_guest_fork_saved_fsbase != 0) {
        if (knl_current_task != 0) {
            knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
    }
    bfree_coop_as_switch_to(1);
    g_coop_child_blocked = 0;
    g_bfree_sysret_exec_rsp = g_coop_child_rsp;
    g_bfree_sysret_exec_rcx = g_coop_child_rcx;
    g_bfree_sysret_exec_r11 = g_coop_child_r11;
    g_bfree_fork_saved_rbx = g_coop_child_rbx;
    g_bfree_fork_saved_rbp = g_coop_child_rbp;
    g_bfree_fork_saved_r12 = g_coop_child_r12;
    g_bfree_fork_saved_r13 = g_coop_child_r13;
    g_bfree_fork_saved_r14 = g_coop_child_r14;
    g_bfree_fork_saved_r15 = g_coop_child_r15;
    g_bfree_fork_saved_rdx = g_coop_child_rdx;
    bfree_coop_arm_child_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

/* After a successful syscall that already mutated state: switch peer, resume
 * this side later with RAX=ret and RIP past syscall (no duplicate I/O). */
static long bfree_coop_yield_to_parent_done(long ret)
{
    bfree_coop_save_child_user();
    g_coop_child_resume_rax = (uint64_t)(long)ret;
    g_coop_child_resume_mode = 2;
    g_coop_child_blocked = 1;
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_parent();
    }
    bfree_coop_fd_switch_to(0);
    bfree_coop_as_switch_to(0);
    bfree_coop_publish_parent_callee_saved();
    g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
    g_bfree_sysret_exec_rcx = g_bfree_fork_saved_rcx;
    g_bfree_sysret_exec_r11 = g_bfree_fork_saved_r11;
    bfree_coop_arm_parent_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

static long bfree_coop_yield_to_child_done(long ret)
{
    g_bfree_fork_saved_rcx = g_bfree_user_sysret_rcx;
    g_bfree_fork_saved_r11 = g_bfree_user_sysret_r11;
    g_bfree_fork_saved_rsp = g_bfree_user_sysret_rsp;
    bfree_coop_save_parent_callee_saved();
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_child();
    }
    g_coop_parent_resume_rax = (uint64_t)(long)ret;
    g_coop_parent_resume_mode = 2;
    bfree_coop_fd_switch_to(1);
    bfree_coop_as_switch_to(1);
    g_coop_child_blocked = 0;
    g_bfree_sysret_exec_rsp = g_coop_child_rsp;
    g_bfree_sysret_exec_rcx = g_coop_child_rcx;
    g_bfree_sysret_exec_r11 = g_coop_child_r11;
    g_bfree_fork_saved_rbx = g_coop_child_rbx;
    g_bfree_fork_saved_rbp = g_coop_child_rbp;
    g_bfree_fork_saved_r12 = g_coop_child_r12;
    g_bfree_fork_saved_r13 = g_coop_child_r13;
    g_bfree_fork_saved_r14 = g_coop_child_r14;
    g_bfree_fork_saved_r15 = g_coop_child_r15;
    g_bfree_fork_saved_rdx = g_coop_child_rdx;
    bfree_coop_arm_child_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

bfree_coop_fd_snap_init();
    if (copy_as) {
        /*
         * AS-copy fork: return to parent immediately so `cmd &` / pipelines that
         * use fork (not vfork) can progress. Child is parked until parent
         * blocks (stdin/wait/pipe) and yields.
         *
         * fork_enter switched CR3 to the child AS — switch back so the parent
         * resumes on its own page table (H02).
         */
        bfree_coop_save_child_user();
        g_coop_child_blocked = 1;
        bfree_coop_fd_switch_to(0);
        bfree_coop_as_switch_to(0);
        g_coop_parent_started = 1;
        g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
        g_bfree_sysret_exec_rcx = g_bfree_fork_saved_rcx;
        g_bfree_sysret_exec_r11 = g_bfree_fork_saved_r11;
        g_bfree_fork_parent_ret = (uint64_t)(long)g_guest_fork_pid;
        return (long)-4092; /* BFREE_SYSRET_COOP_SWITCH */
    }
    return 0; /* vfork: continue as child; parent resumes on child exit */
}

/* Blocking wait with a live AS-copy (parent-first) child: always schedule it.
 * A too-strict check (side/blocked) used to fall into sti;hlt — child never
 * runs, stage2 pipe fork then EAGAINs (pid still RUNNING). */
static long bfree_coop_wait_maybe_yield_child(long status_ptr, long waitid_infop,
                                              int is_waitid)
{
    if (!(g_guest_fork_was_as_copy || g_coop_parent_started)) {
        return 0;
    }
    if (!bfree_process_child_active() && !g_guest_fork_active) {
        return 0;
    }
    g_guest_fork_active = 1;
    g_coop_child_blocked = 1;
    if (g_coop_side != 0) {
        bfree_coop_fd_switch_to(0);
        bfree_coop_as_switch_to(0);
    }
    if (is_waitid) {
        g_guest_wait_status_ptr = 0;
        g_guest_waitid_infop = waitid_infop;
        g_guest_waitid_active = 1;
    } else {
        g_guest_wait_status_ptr = status_ptr;
        g_guest_waitid_active = 0;
    }
    return bfree_coop_yield_to_child();
}

static long sys_linux_waitpid(long pid, long status_ptr, long options)
{
    int status = 0;
    long rc;
    long yr;

    for (;;) {
        rc = bfree_process_wait4(pid,
            (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) ? &status : 0,
            (int)options);
        if (rc > 0) {
            g_guest_fork_status_ready = 0;
            if (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) {
                *(int *)(uintptr_t)status_ptr = status;
            }
            return rc;
        }
        if (rc < 0) {
            return rc; /* ECHILD */
        }
        if (((unsigned)options & 1U) != 0U) { /* WNOHANG */
            return 0;
        }
        yr = bfree_coop_wait_maybe_yield_child(status_ptr, 0, 0);
        if (yr != 0) {
            return yr;
        }
        /* Legacy: shared-AS vfork edge paths. */
        if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked) {
            g_guest_wait_status_ptr = status_ptr;
            g_guest_waitid_active = 0;
            return bfree_coop_yield_to_child();
        }
        {
            int er = bfree_guest_sig_take_eintr();
            if (er < 0) {
                return er;
            }
        }
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

static long bfree_guest_stop_from_fork(int sig)
{
    int as_copy = g_guest_fork_was_as_copy;
    int pid = g_guest_fork_pid;
    int sess = g_coop_session;
    uint64_t parent_fs = g_guest_fork_saved_fsbase;
    int stsig = sig & 0x7f;

    if (stsig == 0) {
        stsig = 20;
    }
    if (g_coop_side == 1 && sess >= 0) {
        bfree_coop_session_park_globals(sess);
    }
    (void)bfree_process_stop_pid(pid, stsig);
    bfree_guest_sig_raise(17); /* SIGCHLD */
    bfree_coop_fd_switch_to(0);
    if (as_copy) {
        bfree_coop_as_switch_to(0);
        if (g_guest_parent_parked_heap_valid) {
            bfree_guest_load_heap_cwd(g_guest_parent_parked_cwd,
                                      g_guest_parent_parked_heap_next,
                                      g_guest_parent_parked_brk);
        }
    } else {
        /* Shared-AS vfork stop: restore frozen parent stack like exit. */
        bfree_guest_vfork_stack_restore();
        bfree_guest_load_heap_cwd(g_guest_fork_saved_cwd,
                                  g_guest_fork_saved_heap_next,
                                  g_guest_fork_saved_brk);
    }
    g_coop_side = 0;
    g_coop_child_blocked = 1;
    g_coop_parent_started = 1;
    g_guest_fork_active = 1;
    g_guest_fork_pid = pid;
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = parent_fs;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, parent_fs);
    g_bfree_sysret_exec_cr3 = 0;
    bfree_coop_publish_parent_resume();
    if (g_guest_wait_status_ptr != 0 &&
        bfree_user_ptr_mapped(g_guest_wait_status_ptr)) {
        *(int *)(uintptr_t)g_guest_wait_status_ptr = (stsig << 8) | 0x7f;
        g_guest_wait_status_ptr = 0;
        {
            int slot = bfree_process_slot_of_pid(pid);
            if (slot >= 0) {
                /* Reported via wait ptr — clear pending once. */
                (void)bfree_process_stop_pid(pid, stsig);
            }
        }
        /* stop_pending was set; clear by a dedicated path — re-stop would
         * set pending again. Use cont+stop? Better: wait4 already; just
         * leave pending for WUNTRACED waiters that didn't use status_ptr. */
    }
    if (g_guest_waitid_active) {
        long infop = g_guest_waitid_infop;
        g_guest_waitid_active = 0;
        g_guest_waitid_infop = 0;
        if (infop != 0 && bfree_user_ptr_mapped(infop)) {
            bfree_siginfo_wait_t *si = (bfree_siginfo_wait_t *)(uintptr_t)infop;
            si->si_signo = BFREE_SIGCHLD;
            si->si_errno = 0;
            si->si_code = 5; /* CLD_STOPPED */
            si->si_pid = pid;
            si->si_uid = 0;
            si->si_status = stsig;
            si->si_utime = 0;
            si->si_stime = 0;
        }
    }
    /* Parent rax = stopped child pid when this was a wait yield. */
    if (g_bfree_fork_parent_ret == 0) {
        g_bfree_fork_parent_ret = (uint64_t)(long)pid;
    }
    return BFREE_SYSRET_FORK_PARENT;
}

static long bfree_guest_exit_from_fork(long status)
{
    int *cleartid;
    size_t i;
    /* Use enter-time AS-copy flag — NOT has_private_as (vfork+exec sets that). */
    int as_copy = g_guest_fork_was_as_copy;

    bfree_process_exit_child((int)status);
    g_guest_fork_active = 0;
    g_guest_fork_was_as_copy = 0;
    g_guest_fork_status = (int)(status & 0xff);
    g_guest_fork_status_ready = 1;
    /* B-Free: SIGCHLD on child exit */
    bfree_guest_sig_raise(17);
    bfree_coop_fd_switch_to(0);
    g_coop_child_blocked = 0;
    /* Shared fd table: a pipeline child may leave stdin/stdout wired to a
     * pipe. Restore the shell's console before the parent resumes. */
    bfree_guest_stdio_heal_pipes();
    for (i = 0; i < sizeof(g_guest_cwd); ++i) {
        g_guest_cwd[i] = g_guest_fork_saved_cwd[i];
        if (g_guest_fork_saved_cwd[i] == '\0') {
            break;
        }
    }
    g_guest_cwd[sizeof(g_guest_cwd) - 1U] = '\0';
    g_guest_heap_next = g_guest_fork_saved_heap_next;
    g_guest_brk = g_guest_fork_saved_brk;
    /* AS-copy: parent stack was never shared — do not poke the vfork stack
     * save over the parent's progressed frames (seq-fork echo|cat).
     * vfork (+exec): parent was frozen; always restore the snapshot. */
    if (!as_copy) {
        bfree_guest_vfork_stack_restore();
    }
    /* Child execve cleared FS/TLS; restore the frozen parent's TLS base. */
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
