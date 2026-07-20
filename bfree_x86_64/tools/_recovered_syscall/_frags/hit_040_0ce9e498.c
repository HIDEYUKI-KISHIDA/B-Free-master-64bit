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