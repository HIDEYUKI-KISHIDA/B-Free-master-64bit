    /* Parent may have progressed (AS-copy); publish parked parent SYSRET. */
    bfree_coop_publish_parent_resume();
    if (g_guest_wait_status_ptr != 0 &&
        bfree_user_ptr_mapped(g_guest_wait_status_ptr)) {
        *(int *)(uintptr_t)g_guest_wait_status_ptr = (g_guest_fork_status & 0xff) << 8;
        g_guest_wait_status_ptr = 0;
    }
    if (g_guest_waitid_active) {
        long infop = g_guest_waitid_infop;
        int st = (g_guest_fork_status & 0xff) << 8;
        g_guest_waitid_active = 0;
        g_guest_waitid_infop = 0;
        if (infop != 0 && bfree_user_ptr_mapped(infop)) {
            bfree_siginfo_wait_t *si = (bfree_siginfo_wait_t *)(uintptr_t)infop;
            si->si_signo = BFREE_SIGCHLD;
            si->si_errno = 0;
            si->si_code = BFREE_CLD_EXITED;
            si->__pad0 = 0;
            si->si_pid = g_guest_fork_pid;
            si->si_uid = 0;
            si->si_status = (st >> 8) & 0xff;
            si->si_utime = 0;
            si->si_stime = 0;
        }
        g_bfree_fork_parent_ret = 0; /* waitid returns 0 */
    } else {
        g_bfree_fork_parent_ret = (uint64_t)(long)g_guest_fork_pid;
    }
    uart_puts("[VFORK] parent resume rip=");
    uart_puthex64(g_coop_parent_rcx);
    uart_puts(" rsp=");
    uart_puthex64(g_coop_parent_rsp);
    uart_puts(" rdx=");
    uart_puthex64(g_coop_parent_rdx);
    uart_puts(" fs=");
    uart_puthex64(g_guest_fork_saved_fsbase);
    uart_puts("\n");
    return BFREE_SYSRET_FORK_PARENT;
}