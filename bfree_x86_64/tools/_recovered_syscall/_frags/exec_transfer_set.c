    g_bfree_sysret_exec_rsp = user_rsp;
    g_bfree_exec_transfer_rip = (uint64_t)(uintptr_t)entry;
    g_bfree_sysret_exec_rcx = g_bfree_exec_transfer_rip;
    g_bfree_sysret_exec_r11 = 0x202ULL;
    /* Child: switch CR3 with RSP in assembly. Standalone: already on task CR3. */
    g_bfree_sysret_exec_cr3 = use_private_as ? (uint64_t)(uintptr_t)child_pt : 0;
    if (g_bfree_exec_transfer_rip == 0ULL) {
        uart_puts("[ELF] FATAL: exec_transfer_rip=0\n");
        if (use_private_as) {
            knl_current_task->page_table_base = parent_pt;
            bfree_process_exit_restore_as();
        }
        return -2;
    }