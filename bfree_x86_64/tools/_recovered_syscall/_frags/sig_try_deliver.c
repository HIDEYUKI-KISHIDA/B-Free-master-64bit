/* Signal dispositions: 0=DFL, 1=IGN, 2=CATCH (user handler). */
#define BFREE_SIG_DFL   0
#define BFREE_SIG_IGN   1
#define BFREE_SIG_CATCH 2
#define BFREE_NSIG      64
#define BFREE_SA_SIGINFO 0x4UL
static uint8_t g_guest_sig_disp[BFREE_NSIG];
static void *g_guest_sig_handler[BFREE_NSIG];
static void *g_guest_sig_restorer[BFREE_NSIG];
static unsigned long g_guest_sig_flags[BFREE_NSIG];
static uint32_t g_guest_uid;
static uint32_t g_guest_euid;
static uint32_t g_guest_gid;
static uint32_t g_guest_egid;

/* B-Free: real POSIX signal pending/mask */
static uint64_t g_guest_sig_pending;
static uint64_t g_guest_sig_mask;
static uint64_t g_guest_alarm_deadline_us;
static int g_guest_alarm_armed;
static void *g_guest_robust_list_head;
static size_t g_guest_robust_list_len;
static int g_guest_flock_holder[BFREE_GUEST_VFILE_SLOTS]; /* 0 free, else fd+1 */

/*
 * Minimal one-level CATCH delivery: redirect SYSRET to sa_handler with rdi=sig
 * and restorer as return address; rt_sigreturn restores interrupted rax/rip/rsp.
 * Remaining gaps: no full rt_sigframe / ucontext, no SA_SIGINFO, no sa_mask
 * apply, no nested delivery, no altstack.
 */
static int g_sig_deliver_sig;
static int g_sig_in_handler;
static uint64_t g_sig_saved_rax;
static uint64_t g_sig_saved_rip;
static uint64_t g_sig_saved_rsp;
static uint64_t g_sig_saved_rflags;

static void bfree_guest_sig_raise(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return;
    }
    if (sig != 9 && g_guest_sig_disp[sig] == BFREE_SIG_IGN) {
        return;
    }
    g_guest_sig_pending |= (1ULL << (unsigned)(sig - 1));
}

static int bfree_guest_sig_is_blocked(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return 0;
    }
    return (g_guest_sig_mask & (1ULL << (unsigned)(sig - 1))) != 0ULL;
}

static int bfree_sysret_is_magic(long r)
{
    return r == BFREE_SYSRET_EXEC_TRANSFER ||
           r == BFREE_SYSRET_FORK_PARENT ||
           r == (long)-4092 ||
           r == BFREE_SYSRET_SIGNAL;
}

static void bfree_guest_sig_arm_catch(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return;
    }
    if (g_sig_in_handler || g_sig_deliver_sig != 0) {
        return;
    }
    if (g_guest_sig_disp[sig] != BFREE_SIG_CATCH) {
        return;
    }
    g_sig_deliver_sig = sig;
}

static int bfree_guest_sig_take_eintr(void)
{
    const int candidates[] = { 2, 14, 13, 17, 15, 1, 10 };
    int i;
    for (i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        int sig = candidates[i];
        uint64_t bit = 1ULL << (unsigned)(sig - 1);
        if ((g_guest_sig_pending & bit) == 0ULL) {
            continue;
        }
        if (bfree_guest_sig_is_blocked(sig)) {
            continue;
        }
        if (sig != 9 && g_guest_sig_disp[sig] == BFREE_SIG_IGN) {
            g_guest_sig_pending &= ~bit;
            continue;
        }
        g_guest_sig_pending &= ~bit;
        if (g_guest_sig_disp[sig] == BFREE_SIG_CATCH) {
            bfree_guest_sig_arm_catch(sig);
        }
        return -4; /* EINTR to caller; CATCH may redirect via try_deliver */
    }
    return 0;
}

static void bfree_guest_sig_arm_pending_catch(void)
{
    const int candidates[] = { 2, 14, 13, 17, 15, 1, 10 };
    int i;

    if (g_sig_in_handler || g_sig_deliver_sig != 0) {
        return;
    }
    for (i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        int sig = candidates[i];
        uint64_t bit = 1ULL << (unsigned)(sig - 1);
        if ((g_guest_sig_pending & bit) == 0ULL) {
            continue;
        }
        if (bfree_guest_sig_is_blocked(sig)) {
            continue;
        }
        if (g_guest_sig_disp[sig] != BFREE_SIG_CATCH) {
            continue;
        }
        g_guest_sig_pending &= ~bit;
        g_sig_deliver_sig = sig;
        return;
    }
}

static long bfree_guest_sig_try_deliver(long syscall_ret)
{
    int sig;
    void *handler;
    void *restorer;
    uint64_t rsp;
    uint64_t *slot;

    if (bfree_sysret_is_magic(syscall_ret)) {
        return syscall_ret;
    }
    bfree_guest_sig_arm_pending_catch();
    if (g_sig_in_handler || g_sig_deliver_sig == 0) {
        return syscall_ret;
    }

    sig = g_sig_deliver_sig;
    g_sig_deliver_sig = 0;
    handler = g_guest_sig_handler[sig];
    restorer = g_guest_sig_restorer[sig];

    /* Require classic sa_handler + restorer; SA_SIGINFO needs a full frame. */
    if (g_guest_sig_disp[sig] != BFREE_SIG_CATCH ||
        handler == 0 || handler == (void *)(uintptr_t)1 ||
        restorer == 0 ||
        (g_guest_sig_flags[sig] & BFREE_SA_SIGINFO) != 0UL ||
        !bfree_user_ptr_mapped((long)(uintptr_t)handler) ||
        !bfree_user_ptr_mapped((long)(uintptr_t)restorer)) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }

    rsp = g_bfree_user_sysret_rsp;
    if (rsp < 16ULL) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }
    rsp -= 8ULL;
    if (!bfree_user_vaddr_mapped(rsp)) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }
    slot = (uint64_t *)(uintptr_t)rsp;
    *slot = (uint64_t)(uintptr_t)restorer;

    g_sig_saved_rax = (uint64_t)(int64_t)syscall_ret;
    g_sig_saved_rip = g_bfree_user_sysret_rcx;
    g_sig_saved_rsp = g_bfree_user_sysret_rsp;
    g_sig_saved_rflags = g_bfree_user_sysret_r11;
    g_sig_in_handler = 1;

    g_bfree_sysret_exec_rsp = rsp;
    g_bfree_sysret_exec_rcx = (uint64_t)(uintptr_t)handler;
    g_bfree_sysret_exec_r11 = g_bfree_user_sysret_r11 | 0x200ULL;
    g_bfree_sysret_exec_rdi = (uint64_t)(unsigned)sig;
    g_bfree_sysret_exec_rsi = 0;
    g_bfree_sysret_exec_rdx = 0;
    g_bfree_sysret_exec_cr3 = 0;
    g_bfree_sysret_sig_rax = 0;
    return BFREE_SYSRET_SIGNAL;
}