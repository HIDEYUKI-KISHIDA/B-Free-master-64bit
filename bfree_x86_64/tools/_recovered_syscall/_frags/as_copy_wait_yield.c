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