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