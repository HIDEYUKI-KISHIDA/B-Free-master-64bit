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