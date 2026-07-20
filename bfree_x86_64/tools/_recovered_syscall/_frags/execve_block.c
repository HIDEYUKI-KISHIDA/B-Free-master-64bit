/* vfork child: load BusyBox into a private address space so the suspended
 * parent's mappings survive. Standalone execve (no fork): replace the current
 * image in-place (shell `exec`, applet re-entry). */
static long sys_linux_execve(long path_ptr, long argv_ptr, long envp_ptr)
{
    static const char *const k_default_env[] = {
        "USER=root",
        "LOGNAME=root",
        "HOME=/root",
        "HOSTNAME=bfree",
        "PATH=/bin:/usr/bin:.",
        "SHELL=/bin/sh",
        "TERM=linux",
        "PS1=root@bfree:# "
    };
    char path[256];
    char argv_buf[16][256];
    char env_buf[16][256];
    const char *argv_ptrs[16];
    const char *env_ptrs[16];
    bfree_loaded_elf_info_t elf;
    page_table_t *child_pt = 0;
    page_table_t *load_pt = 0;
    page_table_t *parent_pt = 0;
    void *entry = 0;
    uint64_t user_rsp = 0;
    uint64_t stack_top;
    int argc = 0;
    int envc = 0;
    int i;
    int use_private_as = 0;
    int is_child;
    int ld;

    is_child = g_guest_fork_active && bfree_process_child_active();

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    if (bfree_copy_user_strarray(argv_ptr, argv_buf, 16, &argc) != 0 || argc <= 0) {
        return -14;
    }
    /* musl busybox expects argv[0]=/busybox.elf when re-entering from execve. */
    if (argc + 1 <= 16) {
        int j;
        for (j = argc; j >= 1; --j) {
            memcpy(argv_buf[j], argv_buf[j - 1], sizeof(argv_buf[j]));
        }
        memcpy(argv_buf[0], g_guest_busybox_exe_path, sizeof(argv_buf[0]));
        argv_buf[0][sizeof(argv_buf[0]) - 1] = '\0';
        ++argc;
    }
    for (i = 0; i < argc; ++i) {
        argv_ptrs[i] = argv_buf[i];
    }
    if (envp_ptr != 0 &&
        bfree_copy_user_strarray(envp_ptr, env_buf, 16, &envc) != 0) {
        return -14;
    }
    if (envc <= 0) {
        envc = 8;
        for (i = 0; i < envc; ++i) {
            env_ptrs[i] = k_default_env[i];
        }
    } else {
        for (i = 0; i < envc; ++i) {
            env_ptrs[i] = env_buf[i];
        }
    }

    if (!knl_current_task || !knl_current_task->page_table_base) {
        return -1;
    }
    parent_pt = (page_table_t *)knl_current_task->page_table_base;

    /* Kernel-only bookkeeping is safe for both paths. */
    g_guest_proc_maps_off = 0;
    g_guest_proc_pid_stat_off = 0;
    g_guest_proc_cmdline_off = 0;
    g_guest_proc_meminfo_off = 0;
    g_guest_proc_uptime_off = 0;
    g_guest_proc_loadavg_off = 0;
    g_guest_proc_cpustat_off = 0;
    g_guest_proc_status_off = 0;
    g_guest_proc_mounts_off = 0;
    g_guest_proc_pid2_stat_off = 0;
    g_guest_proc_pid2_cmdline_off = 0;
    g_guest_etc_passwd_off = 0;
    g_guest_etc_group_off = 0;
    g_guest_etc_profile_off = 0;
    g_guest_etc_motd_off = 0;
    bfree_guest_proc_maps_select_busybox(1);

    if (is_child) {
        /*
         * Private AS: map ELF into child_pt while CR3 stays on the parent.
         * load_elf_image no longer activates page_table_base (that #PF'd with
         * parent user RSP). Stack helpers temporarily use TCB→child_pt; CR3
         * switches with the new RSP in syscall_entry.S.
         */
        if (bfree_process_exec_commit_as(&child_pt) != 0 || child_pt == 0) {
            return -12; /* ENOMEM */
        }
        use_private_as = 1;
        load_pt = child_pt;
        bfree_guest_heap_reset();
        bfree_guest_execve_reset_subsystems(1);
        ld = load_elf_image("busybox.elf", &entry, load_pt);
        if (ld != 0 || entry == 0) {
            bfree_process_exit_restore_as();
            return -2;
        }
        bfree_loaded_elf_info_get(&elf);
        knl_current_task->page_table_base = child_pt;
    } else {
        /* Standalone: replace current image (no parent to preserve). */
        bfree_guest_heap_reset();
        timer_purge_all();
        bfree_timerfd_purge_all();
        bfree_guest_execve_reset_subsystems(1);
        load_pt = parent_pt;
        ld = load_elf_image("busybox.elf", &entry, load_pt);
        if (ld != 0 || entry == 0) {
            bfree_loaded_elf_info_get(&elf);
            if (!elf.valid || elf.entry == 0) {
                return -2;
            }
            entry = (void *)(uintptr_t)elf.entry;
        } else {
            bfree_loaded_elf_info_get(&elf);
        }
    }

#define BFREE_VFORK_EXEC_STACK_SLOT_PAGES 32

    stack_top = knl_current_task->user_stack_top;
    if (stack_top == 0) {
        stack_top = BFREE_USER_STACK_TOP_DEFAULT;
    }
    {
        uint64_t exec_stack_top =
            stack_top - (uint64_t)BFREE_VFORK_EXEC_STACK_SLOT_PAGES * PAGE_SIZE;

        if (exec_stack_top < BFREE_USER_STACK_MIN_VADDR + PAGE_SIZE) {
            if (use_private_as) {
                knl_current_task->page_table_base = parent_pt;
                bfree_process_exit_restore_as();
            }
            return -1;
        }
        if (bfree_user_stack_ensure_pages(stack_top,
                BFREE_USER_STACK_PAGES_BUSYBOX + BFREE_VFORK_EXEC_STACK_SLOT_PAGES) != 0) {
            if (use_private_as) {
                knl_current_task->page_table_base = parent_pt;
                bfree_process_exit_restore_as();
            }
            return -1;
        }
        if (bfree_user_exec_prepare_musl_stack_argv(exec_stack_top, argc, argv_ptrs, envc,
                                                    env_ptrs, &elf, &user_rsp) != 0) {
            if (use_private_as) {
                knl_current_task->page_table_base = parent_pt;
                bfree_process_exit_restore_as();
            }
            return -1;
        }
    }

    (void)path;
    bfree_enable_user_fpu();
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = 0;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, 0);
    g_bfree_sysret_exec_rsp = user_rsp;
    g_bfree_sysret_exec_rcx = (uint64_t)(uintptr_t)entry;
    g_bfree_sysret_exec_r11 = 0x202ULL;
    /* Child: switch CR3 with RSP in assembly. Standalone: already on task CR3. */
    g_bfree_sysret_exec_cr3 = use_private_as ? (uint64_t)(uintptr_t)child_pt : 0;
    return BFREE_SYSRET_EXEC_TRANSFER;
}