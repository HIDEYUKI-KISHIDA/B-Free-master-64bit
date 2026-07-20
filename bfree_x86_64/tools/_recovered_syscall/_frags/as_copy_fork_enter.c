    return BFREE_SYSRET_FORK_PARENT;
}

/* H06: job-control stop — keep child AS/session, yield parent with WIFSTOPPED. */
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

/* Blocking wait with a live coop child: always schedule it. Never fall through
 * to sti;hlt while a VFORK/RUNNING child exists — that left AS-copy pipe stage1
 * parked forever and stage2 fork got EAGAIN (pid still RUNNING). */