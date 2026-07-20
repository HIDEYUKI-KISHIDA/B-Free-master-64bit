#!/usr/bin/env python3
"""Repair H01: soft-stub regex deleted the real raise/try_deliver/sigreturn bodies."""
from pathlib import Path

PATH = Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/kernel/sysmain/syscall.c")
text = PATH.read_text(encoding="utf-8", errors="replace")

BODIES = r'''
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

static void bfree_guest_sig_arm_catch(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return;
    }
    if (g_sig_in_handler || g_sig_deliver_sig != 0) {
        return; /* nested CATCH residual */
    }
    if (g_guest_sig_disp[sig] != BFREE_SIG_CATCH) {
        return;
    }
    g_sig_deliver_sig = sig;
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
        return -4; /* EINTR */
    }
    return 0;
}

static long bfree_guest_sig_try_deliver(long syscall_ret)
{
    int sig;
    void *handler;
    void *restorer;
    uint64_t rsp;
    uint64_t frame_base;
    bfree_rt_sigframe_t *frame;

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

    if (g_guest_sig_disp[sig] != BFREE_SIG_CATCH ||
        handler == 0 || handler == (void *)(uintptr_t)1 ||
        restorer == 0 ||
        !bfree_user_ptr_mapped((long)(uintptr_t)handler) ||
        !bfree_user_ptr_mapped((long)(uintptr_t)restorer)) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }

    rsp = g_bfree_user_sysret_rsp;
    if (rsp < (uint64_t)sizeof(bfree_rt_sigframe_t) + 16ULL) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }
    rsp &= ~15ULL;
    frame_base = rsp - (uint64_t)sizeof(bfree_rt_sigframe_t);
    if (!bfree_user_range_mapped(frame_base, sizeof(bfree_rt_sigframe_t))) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }
    frame = (bfree_rt_sigframe_t *)(uintptr_t)frame_base;
    memset(frame, 0, sizeof(*frame));
    frame->pretcode = (uint64_t)(uintptr_t)restorer;
    frame->info.si_signo = sig;
    frame->info.si_code = 0;

    g_sig_saved_rax = (uint64_t)(int64_t)syscall_ret;
    g_sig_saved_rdi = 0;
    g_sig_saved_rsi = 0;
    g_sig_saved_rdx = g_bfree_user_sysret_rdx;
    g_sig_saved_rbx = g_bfree_user_sysret_rbx;
    g_sig_saved_rbp = g_bfree_user_sysret_rbp;
    g_sig_saved_r12 = g_bfree_user_sysret_r12;
    g_sig_saved_r13 = g_bfree_user_sysret_r13;
    g_sig_saved_r14 = g_bfree_user_sysret_r14;
    g_sig_saved_r15 = g_bfree_user_sysret_r15;
    g_sig_saved_rip = g_bfree_user_sysret_rcx;
    g_sig_saved_rsp = g_bfree_user_sysret_rsp;
    g_sig_saved_rflags = g_bfree_user_sysret_r11;
    g_sig_saved_mask = g_guest_sig_mask;

    frame->uc.mc.r12 = g_sig_saved_r12;
    frame->uc.mc.r13 = g_sig_saved_r13;
    frame->uc.mc.r14 = g_sig_saved_r14;
    frame->uc.mc.r15 = g_sig_saved_r15;
    frame->uc.mc.rdi = g_sig_saved_rdi;
    frame->uc.mc.rsi = g_sig_saved_rsi;
    frame->uc.mc.rbp = g_sig_saved_rbp;
    frame->uc.mc.rbx = g_sig_saved_rbx;
    frame->uc.mc.rdx = g_sig_saved_rdx;
    frame->uc.mc.rax = g_sig_saved_rax;
    frame->uc.mc.rsp = g_sig_saved_rsp;
    frame->uc.mc.rip = g_sig_saved_rip;
    frame->uc.mc.efl = g_sig_saved_rflags;
    frame->uc.mc.oldmask = g_sig_saved_mask;
    frame->uc.uc_sigmask = g_sig_saved_mask;

    g_guest_sig_mask |= g_guest_sig_sa_mask[sig];
    if ((g_guest_sig_flags[sig] & BFREE_SA_NODEFER) == 0UL) {
        g_guest_sig_mask |= (1ULL << (unsigned)(sig - 1));
    }
    g_guest_sig_mask &= ~((1ULL << 8) | (1ULL << 18));
    g_sig_mask_pushed = 1;
    g_sig_in_handler = 1;

    g_bfree_sysret_exec_rsp = frame_base;
    g_bfree_sysret_exec_rcx = (uint64_t)(uintptr_t)handler;
    g_bfree_sysret_exec_r11 = g_bfree_user_sysret_r11 | 0x200ULL;
    g_bfree_sysret_exec_rdi = (uint64_t)(unsigned)sig;
    if ((g_guest_sig_flags[sig] & BFREE_SA_SIGINFO) != 0UL) {
        g_bfree_sysret_exec_rsi = frame_base + BFREE_OFFSETOF(bfree_rt_sigframe_t, info);
        g_bfree_sysret_exec_rdx = frame_base + BFREE_OFFSETOF(bfree_rt_sigframe_t, uc);
    } else {
        g_bfree_sysret_exec_rsi = 0;
        g_bfree_sysret_exec_rdx = 0;
    }
    g_bfree_sysret_exec_cr3 = 0;
    g_bfree_sysret_sig_rax = 0;
    g_bfree_sig_saved_rbx = g_sig_saved_rbx;
    g_bfree_sig_saved_rbp = g_sig_saved_rbp;
    g_bfree_sig_saved_r12 = g_sig_saved_r12;
    g_bfree_sig_saved_r13 = g_sig_saved_r13;
    g_bfree_sig_saved_r14 = g_sig_saved_r14;
    g_bfree_sig_saved_r15 = g_sig_saved_r15;
    g_bfree_sig_saved_rdx = g_sig_saved_rdx;
    return BFREE_SYSRET_SIGNAL;
}

static long sys_linux_rt_sigreturn(void)
{
    if (!g_sig_in_handler) {
        return -22;
    }
    g_sig_in_handler = 0;
    if (g_sig_mask_pushed) {
        g_guest_sig_mask = g_sig_saved_mask;
        g_sig_mask_pushed = 0;
    }
    g_bfree_sysret_exec_rsp = g_sig_saved_rsp;
    g_bfree_sysret_exec_rcx = g_sig_saved_rip;
    g_bfree_sysret_exec_r11 = g_sig_saved_rflags | 0x200ULL;
    g_bfree_sysret_exec_rdi = g_sig_saved_rdi;
    g_bfree_sysret_exec_rsi = g_sig_saved_rsi;
    g_bfree_sysret_exec_rdx = g_sig_saved_rdx;
    g_bfree_sysret_exec_cr3 = 0;
    g_bfree_sysret_sig_rax = g_sig_saved_rax;
    g_bfree_sig_saved_rbx = g_sig_saved_rbx;
    g_bfree_sig_saved_rbp = g_sig_saved_rbp;
    g_bfree_sig_saved_r12 = g_sig_saved_r12;
    g_bfree_sig_saved_r13 = g_sig_saved_r13;
    g_bfree_sig_saved_r14 = g_sig_saved_r14;
    g_bfree_sig_saved_r15 = g_sig_saved_r15;
    g_bfree_sig_saved_rdx = g_sig_saved_rdx;
    return BFREE_SYSRET_SIGNAL;
}

#endif /* BFREE_H01_SIGFRAME_WIRED */

#ifndef BFREE_RESTORE_SOFT_BODIES
#define BFREE_RESTORE_SOFT_BODIES 1
'''

needle = "/* H01: sig raise/take_eintr/try_deliver provided above */\n"
if needle not in text:
    raise SystemExit("FAIL: repair needle missing — already fixed?")
# Replace comment + exit_from_fork_signal that wrongly sits inside H01 ifndef,
# through the erroneous #endif of soft bodies.
old = """/* H01: sig raise/take_eintr/try_deliver provided above */
static long bfree_guest_exit_from_fork_signal(int sig)
{
    /* Soft: treat as normal coop child exit with signal status. */
    (void)sig;
    if (g_guest_fork_active) {
        g_guest_fork_status = sig & 0x7f;
        g_guest_fork_status_ready = 1;
        g_guest_fork_active = 0;
    }
    return BFREE_SYSRET_FORK_PARENT;
}
#endif
/* === end restore soft bodies === */

#ifndef BFREE_RESTORE_SOFT_STUBS
"""

new = BODIES + """static long bfree_guest_exit_from_fork_signal(int sig)
{
    /* Soft: treat as normal coop child exit with signal status. */
    (void)sig;
    if (g_guest_fork_active) {
        g_guest_fork_status = sig & 0x7f;
        g_guest_fork_status_ready = 1;
        g_guest_fork_active = 0;
    }
    return BFREE_SYSRET_FORK_PARENT;
}
#endif
/* === end restore soft bodies === */

#ifndef BFREE_RESTORE_SOFT_STUBS
"""

if old not in text:
    raise SystemExit("FAIL: old block not found")
text = text.replace(old, new, 1)
PATH.write_text(text, encoding="utf-8", newline="\n")
print("OK repaired H01 bodies")
