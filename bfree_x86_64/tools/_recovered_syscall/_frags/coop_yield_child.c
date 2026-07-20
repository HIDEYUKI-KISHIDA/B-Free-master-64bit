static long bfree_coop_yield_to_child(void)
{
    /* Park full parent user context before staging child into fork_saved_*. */
    bfree_coop_save_parent_user();
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
    bfree_coop_publish_child_resume();
    bfree_coop_arm_child_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}