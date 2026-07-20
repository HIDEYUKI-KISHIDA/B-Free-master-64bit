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
    bfree_coop_publish_parent_resume();
    bfree_coop_arm_parent_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}