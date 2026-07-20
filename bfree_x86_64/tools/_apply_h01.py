#!/usr/bin/env python3
"""H01: solidify rt_sigframe + g_bfree_sig_saved_* + deliver/sigreturn path."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "kernel" / "sysmain" / "syscall.c"
ENTRY = ROOT / "kernel" / "sysdepend" / "cpu" / "x86_64" / "syscall_entry.S"
text = PATH.read_text(encoding="utf-8", errors="replace")
entry = ENTRY.read_text(encoding="utf-8", errors="replace")
orig = text

MARKER = "BFREE_H01_SIGFRAME_WIRED"

if MARKER in text:
    print("skip already H01")
else:
    # --- 1) Globals for asm handoff (must be non-static) ---
    if "g_bfree_sig_saved_rbx" not in text:
        needle = "uint64_t g_bfree_sysret_sig_rax;\n"
        if needle not in text:
            raise SystemExit("FAIL: sig_rax")
        text = text.replace(
            needle,
            needle
            + "\n/* H01: callee-saved published for BFREE_SYSRET_SIGNAL (syscall_entry.S). */\n"
            + "uint64_t g_bfree_sig_saved_rbx;\n"
            + "uint64_t g_bfree_sig_saved_rbp;\n"
            + "uint64_t g_bfree_sig_saved_r12;\n"
            + "uint64_t g_bfree_sig_saved_r13;\n"
            + "uint64_t g_bfree_sig_saved_r14;\n"
            + "uint64_t g_bfree_sig_saved_r15;\n"
            + "uint64_t g_bfree_sig_saved_rdx;\n",
            1,
        )
        print("OK g_bfree_sig_saved_*")

    # --- 2) Expand signal state near existing g_guest_sig_disp ---
    if "g_guest_sig_handler" not in text:
        needle = "static uint8_t g_guest_sig_disp[BFREE_NSIG];\n"
        if needle not in text:
            raise SystemExit("FAIL: sig_disp")
        text = text.replace(
            needle,
            needle
            + "#ifndef BFREE_SA_SIGINFO\n"
            + "#define BFREE_SA_SIGINFO 0x4UL\n"
            + "#define BFREE_SA_NODEFER 0x40000000UL\n"
            + "#endif\n"
            + "static void *g_guest_sig_handler[BFREE_NSIG];\n"
            + "static void *g_guest_sig_restorer[BFREE_NSIG];\n"
            + "static unsigned long g_guest_sig_flags[BFREE_NSIG];\n"
            + "static uint64_t g_guest_sig_sa_mask[BFREE_NSIG];\n"
            + "static uint64_t g_guest_sig_pending;\n"
            + "static uint64_t g_guest_sig_mask;\n",
            1,
        )
        print("OK sig handler/pending state")

    # --- 3) Insert H01 body before soft bodies; replace soft sig helpers ---
    H01 = r'''
/* === H01 rt_sigframe / CATCH deliver (fpstate + nested CATCH still residual) === */
#ifndef BFREE_H01_SIGFRAME_WIRED
#define BFREE_H01_SIGFRAME_WIRED 1

#ifndef BFREE_OFFSETOF
#define BFREE_OFFSETOF(type, member) __builtin_offsetof(type, member)
#endif

static int g_sig_deliver_sig;
static int g_sig_in_handler;
static int g_sig_mask_pushed;
static uint64_t g_sig_saved_rax;
static uint64_t g_sig_saved_rdi;
static uint64_t g_sig_saved_rsi;
static uint64_t g_sig_saved_rdx;
static uint64_t g_sig_saved_rbx;
static uint64_t g_sig_saved_rbp;
static uint64_t g_sig_saved_r12;
static uint64_t g_sig_saved_r13;
static uint64_t g_sig_saved_r14;
static uint64_t g_sig_saved_r15;
static uint64_t g_sig_saved_rip;
static uint64_t g_sig_saved_rsp;
static uint64_t g_sig_saved_rflags;
static uint64_t g_sig_saved_mask;

typedef struct {
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, efl;
    uint64_t csgsfs, err, trapno, oldmask, cr2;
} bfree_sig_mcontext_t;

typedef struct {
    uint64_t pad_uc_flags;
    uint64_t pad_uc_link;
    uint64_t pad_ss_sp;
    uint64_t pad_ss_flags;
    uint64_t pad_ss_size;
    bfree_sig_mcontext_t mc;
    uint64_t uc_sigmask;
} bfree_sig_ucontext_t;

typedef struct {
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    int32_t si_pad;
    uint64_t si_addr;
    int32_t si_status;
    int32_t si_pad2;
    uint64_t si_value;
    uint8_t pad[96];
} bfree_siginfo_min_t;

typedef struct {
    uint64_t pretcode;
    bfree_sig_ucontext_t uc;
    bfree_siginfo_min_t info;
} bfree_rt_sigframe_t;

static int bfree_user_range_mapped(uint64_t base, size_t nbytes)
{
    uint64_t a;
    uint64_t end;

    if (nbytes == 0) {
        return 1;
    }
    if (base + (uint64_t)nbytes < base) {
        return 0;
    }
    end = base + (uint64_t)nbytes;
    for (a = base & ~0xfffULL; a < end; a += 0x1000ULL) {
        if (!bfree_user_vaddr_mapped(a)) {
            return 0;
        }
    }
    return 1;
}

static int bfree_sysret_is_magic(long r)
{
    return r == BFREE_SYSRET_EXEC_TRANSFER ||
           r == BFREE_SYSRET_FORK_PARENT ||
           r == BFREE_SYSRET_COOP_SWITCH ||
           r == BFREE_SYSRET_SIGNAL;
}

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
    g_guest_sig_mask &= ~((1ULL << 8) | (1ULL << 18)); /* never mask KILL/STOP */
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
'''

    soft = "/* === restore soft bodies (compile-only; H02/H01 replace later) === */"
    if soft not in text:
        soft = "#ifndef BFREE_RESTORE_SOFT_BODIES"
    if "BFREE_H01_SIGFRAME_WIRED" not in text:
        # insert before soft bodies
        anchor = "#ifndef BFREE_RESTORE_SOFT_BODIES"
        if anchor not in text:
            raise SystemExit("FAIL soft bodies")
        text = text.replace(anchor, H01 + "\n" + anchor, 1)
        print("OK H01 body")

    # Remove soft sig_raise / take_eintr / try_deliver
    text2, n = re.subn(
        r"static void bfree_guest_sig_raise\(int sig\)\s*\{.*?\n\}\s*"
        r"static int bfree_guest_sig_take_eintr\(void\)\s*\{.*?\n\}\s*"
        r"static long bfree_guest_sig_try_deliver\(long ret\)\s*\{.*?\n\}\s*",
        "/* H01: sig raise/take_eintr/try_deliver provided above */\n",
        text,
        count=1,
        flags=re.S,
    )
    if n:
        text = text2
        print("OK removed soft sig helpers")
    else:
        print("WARN soft sig helpers not removed")

    # Replace soft rt_sigreturn stub
    text = text.replace(
        "static long sys_linux_rt_sigreturn(void) { return 0; }\n",
        "/* H01: sys_linux_rt_sigreturn provided above */\n",
        1,
    )
    print("OK removed soft rt_sigreturn")

    # --- 4) Replace stub rt_sigaction / rt_sigprocmask ---
    old_act = """static long sys_linux_rt_sigaction(long signum, long act, long oldact, long sigsetsize)
{
    (void)signum;
    (void)act;
    (void)oldact;
    (void)sigsetsize;
    return 0;
}

static long sys_linux_rt_sigprocmask(long how, long set, long oldset, long sigsetsize)
{
    (void)how;
    (void)set;
    (void)oldset;
    (void)sigsetsize;
    return 0;
}
"""
    new_act = """static long sys_linux_rt_sigaction(long signum, long act, long oldact, long sigsetsize)
{
    typedef struct {
        void *sa_handler;
        unsigned long sa_flags;
        void *sa_restorer;
        unsigned long sa_mask;
    } bfree_k_sigaction_t;
    bfree_k_sigaction_t *ka;
    int sig = (int)signum;

    (void)sigsetsize;
    if (sig <= 0 || sig >= BFREE_NSIG || sig == 9 || sig == 19) {
        return -22;
    }
    if (oldact != 0) {
        if (!bfree_user_ptr_mapped(oldact)) {
            return -14;
        }
        ka = (bfree_k_sigaction_t *)(uintptr_t)oldact;
        if (g_guest_sig_disp[sig] == BFREE_SIG_IGN) {
            ka->sa_handler = (void *)(uintptr_t)1;
        } else if (g_guest_sig_disp[sig] == BFREE_SIG_CATCH) {
            ka->sa_handler = g_guest_sig_handler[sig];
        } else {
            ka->sa_handler = (void *)0;
        }
        ka->sa_flags = g_guest_sig_flags[sig];
        ka->sa_restorer = g_guest_sig_restorer[sig];
        ka->sa_mask = (unsigned long)g_guest_sig_sa_mask[sig];
    }
    if (act != 0) {
        void *handler;
        if (!bfree_user_ptr_mapped(act)) {
            return -14;
        }
        ka = (bfree_k_sigaction_t *)(uintptr_t)act;
        handler = ka->sa_handler;
        g_guest_sig_flags[sig] = ka->sa_flags;
        g_guest_sig_restorer[sig] = ka->sa_restorer;
        g_guest_sig_sa_mask[sig] = (uint64_t)ka->sa_mask;
        g_guest_sig_handler[sig] = handler;
        if (handler == (void *)0) {
            g_guest_sig_disp[sig] = BFREE_SIG_DFL;
        } else if (handler == (void *)(uintptr_t)1) {
            g_guest_sig_disp[sig] = BFREE_SIG_IGN;
        } else {
            g_guest_sig_disp[sig] = BFREE_SIG_CATCH;
        }
    }
    return 0;
}

static long sys_linux_rt_sigprocmask(long how, long set, long oldset, long sigsetsize)
{
    uint64_t newmask;
    uint64_t old;

    (void)sigsetsize;
    old = g_guest_sig_mask;
    if (oldset != 0) {
        if (!bfree_user_ptr_mapped(oldset)) {
            return -14;
        }
        *(uint64_t *)(uintptr_t)oldset = old;
    }
    if (set == 0) {
        return 0;
    }
    if (!bfree_user_ptr_mapped(set)) {
        return -14;
    }
    newmask = *(uint64_t *)(uintptr_t)set;
    newmask &= ~((1ULL << 8) | (1ULL << 18));
    if (how == 0) { /* SIG_BLOCK */
        g_guest_sig_mask = old | newmask;
    } else if (how == 1) { /* SIG_UNBLOCK */
        g_guest_sig_mask = old & ~newmask;
    } else if (how == 2) { /* SIG_SETMASK */
        g_guest_sig_mask = newmask;
    } else {
        return -22;
    }
    return 0;
}
"""
    if old_act in text:
        text = text.replace(old_act, new_act, 1)
        print("OK rt_sigaction/procmask")
    else:
        print("WARN rt_sigaction stubs not exact")

    # Ensure case 15 rt_sigreturn in dispatch
    if "sys_linux_rt_sigreturn()" not in text:
        # insert near rt_sigaction cases
        needle = "        return sys_linux_rt_sigprocmask(arg1, arg2, arg3, arg4);"
        if needle in text:
            text = text.replace(
                needle,
                needle
                + "\n    case 15: /* rt_sigreturn */\n"
                + "        return sys_linux_rt_sigreturn();",
                1,
            )
            print("OK case 15 rt_sigreturn")
        else:
            print("WARN case 15 insert failed")
    else:
        print("OK rt_sigreturn already dispatched")

PATH.write_text(text, encoding="utf-8", newline="\n")
print("wrote syscall delta", len(text) - len(orig))

# --- entry.S: add .extern for g_bfree_sig_saved_* ---
if "g_bfree_sig_saved_rbx" not in entry or ".extern g_bfree_sig_saved_rbx" not in entry:
    needle = "\t.extern g_bfree_sysret_sig_rax\n"
    if needle not in entry:
        # try without tabs
        needle = ".extern g_bfree_sysret_sig_rax\n"
    extra = (
        needle
        + "\n\t.extern g_bfree_sig_saved_rbx\n"
        + "\t.extern g_bfree_sig_saved_rbp\n"
        + "\t.extern g_bfree_sig_saved_r12\n"
        + "\t.extern g_bfree_sig_saved_r13\n"
        + "\t.extern g_bfree_sig_saved_r14\n"
        + "\t.extern g_bfree_sig_saved_r15\n"
        + "\t.extern g_bfree_sig_saved_rdx\n"
    )
    if ".extern g_bfree_sig_saved_rbx" not in entry:
        if "\t.extern g_bfree_sysret_sig_rax\n" in entry:
            entry = entry.replace("\t.extern g_bfree_sysret_sig_rax\n", extra, 1)
            ENTRY.write_text(entry, encoding="utf-8", newline="\n")
            print("OK entry.S externs")
        else:
            print("WARN entry.S extern insert skipped")
    else:
        print("skip entry externs")
else:
    print("skip entry already")
