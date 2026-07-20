/* Recovered guest CLONE_THREAD helpers (inlined in syscall.c).
 * Source: 97982f27. Related: bfree_guest_thread_*, g_guest_thread_*.
 * Clone dispatch CLONE_THREAD gate may be a separate small patch.
 */

static uintptr_t g_guest_clear_child_tid;
static int g_guest_thread_active;
static int g_guest_thread_tid;
static int g_guest_thread_slots_used;

static void bfree_guest_thread_init(void)
{
    g_guest_thread_active = 0;
    g_guest_thread_tid = 0;
    g_guest_thread_slots_used = 0;
}

static void bfree_guest_futex_wake_user(volatile int *uaddr)
{
    int i;

    if (!uaddr) {
        return;
    }
    for (i = 0; i < BFREE_FUTEX_WAITERS; ++i) {
        if (g_futex_waiter_armed[i] && g_futex_waiter_uaddr[i] == uaddr) {
            g_futex_waiter_armed[i] = 0;
            g_futex_waiter_uaddr[i] = 0;
        }
    }
}

static void bfree_guest_thread_save_parent_ctx(void)
{
    g_bfree_fork_saved_rcx = g_bfree_user_sysret_rcx;
    g_bfree_fork_saved_r11 = g_bfree_user_sysret_r11;
    g_bfree_fork_saved_rsp = g_bfree_user_sysret_rsp;
    g_bfree_fork_saved_rbx = g_bfree_user_sysret_rbx;
    g_bfree_fork_saved_rbp = g_bfree_user_sysret_rbp;
    g_bfree_fork_saved_r12 = g_bfree_user_sysret_r12;
    g_bfree_fork_saved_r13 = g_bfree_user_sysret_r13;
    g_bfree_fork_saved_r14 = g_bfree_user_sysret_r14;
    g_bfree_fork_saved_r15 = g_bfree_user_sysret_r15;
    g_bfree_fork_saved_rdx = g_bfree_user_sysret_rdx;
}

static long bfree_guest_thread_clone(unsigned long flags, long newsp, long ptid, long ctid, long tls)
{
    int tid;
    uint64_t child_rsp;
    uint64_t parent_fs;

    if (g_guest_fork_active || g_guest_thread_active) {
        return -11;
    }
    if (g_guest_thread_slots_used >= BFREE_GUEST_MAX_THREADS) {
        return -11;
    }
    if (newsp == 0 || !bfree_user_ptr_mapped(newsp)) {
        return -14;
    }
    child_rsp = (uint64_t)(uintptr_t)newsp;
    child_rsp &= ~0xFULL;

    tid = g_guest_next_pid++;
    if (tid <= 0) {
        return -11;
    }

    parent_fs = bfree_rdmsr64((uint32_t)BFREE_MSR_FS_BASE);
    g_guest_fork_saved_fsbase = parent_fs;
    bfree_guest_thread_save_parent_ctx();

    if ((flags & 0x00080000UL) != 0UL && tls != 0) { /* CLONE_SETTLS */
        if (!bfree_user_ptr_mapped(tls)) {
            return -14;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, (uint64_t)(uintptr_t)tls);
    }

    if ((flags & 0x00100000UL) != 0UL && ptid != 0 && bfree_user_ptr_mapped(ptid)) {
        *(int *)(uintptr_t)ptid = tid;
    }
    if ((flags & 0x00200000UL) != 0UL && ctid != 0 && bfree_user_ptr_mapped(ctid)) {
        g_guest_clear_child_tid = (uintptr_t)ctid;
        *(int *)(uintptr_t)ctid = tid;
    } else {
        g_guest_clear_child_tid = 0;
    }

    g_guest_thread_active = 1;
    g_guest_thread_tid = tid;
    g_guest_thread_slots_used++;

    g_bfree_sysret_exec_rsp = child_rsp;
    return BFREE_SYSRET_THREAD_CHILD;
}

static long bfree_guest_thread_exit(long status)
{
    int tid;
    int *cleartid;

    (void)status;
    if (!g_guest_thread_active) {
        return -1;
    }
    tid = g_guest_thread_tid;

    if (g_guest_clear_child_tid != 0 &&
        bfree_user_ptr_mapped((long)g_guest_clear_child_tid)) {
        cleartid = (int *)(uintptr_t)g_guest_clear_child_tid;
        *cleartid = 0;
        bfree_guest_futex_wake_user((volatile int *)cleartid);
        g_guest_clear_child_tid = 0;
    }

    g_guest_thread_active = 0;
    g_guest_thread_tid = 0;
    if (g_guest_thread_slots_used > 0) {
        g_guest_thread_slots_used--;
    }

    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
    g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
    g_bfree_fork_parent_ret = (uint64_t)(long)tid;
    return BFREE_SYSRET_FORK_PARENT;
}
