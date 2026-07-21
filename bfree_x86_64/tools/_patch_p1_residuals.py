#!/usr/bin/env python3
"""P1 residuals: restore H17 sigaltstack+SA_ONSTACK; wire H26 CLONE_THREAD gate."""
from pathlib import Path

p = Path(__file__).resolve().parents[1] / "kernel" / "sysmain" / "syscall.c"
t = p.read_text(encoding="utf-8")

# --- defines ---
old_sa = """#ifndef BFREE_SA_SIGINFO
#define BFREE_SA_SIGINFO 0x4UL
#define BFREE_SA_NODEFER 0x40000000UL
#endif
"""
new_sa = """#ifndef BFREE_SA_SIGINFO
#define BFREE_SA_SIGINFO 0x4UL
#define BFREE_SA_NODEFER 0x40000000UL
#define BFREE_SA_ONSTACK 0x08000000UL /* Linux x86_64 */
#define BFREE_SS_ONSTACK 1
#define BFREE_SS_DISABLE 2
#define BFREE_SS_AUTODISARM 0x80000000UL /* accepted, not enforced (H17 residual) */
#define BFREE_MINSIGSTKSZ 2048
#endif
"""
if "BFREE_SA_ONSTACK" not in t:
    if old_sa not in t:
        # try without ifndef wrap already expanded
        raise SystemExit("SA define block missing")
    t = t.replace(old_sa, new_sa, 1)
    print("[ok] SA/SS defines")
else:
    print("[skip] SA/SS defines")

# --- globals ---
if "g_sigalt_sp" not in t:
    needle = "static int g_guest_thread_slots_used;\n"
    add = needle + """
/* H17: registered alternate signal stack */
static void *g_sigalt_sp;
static size_t g_sigalt_size;
static int g_sigalt_disable = 1;
"""
    if needle not in t:
        raise SystemExit("thread_slots needle missing")
    t = t.replace(needle, add, 1)
    print("[ok] g_sigalt_*")
else:
    print("[skip] g_sigalt_*")

# --- bfree_sigalt_onstack after bfree_user_range_mapped ---
if "bfree_sigalt_onstack" not in t:
    # find end of bfree_user_range_mapped
    mi = t.find("static int bfree_user_range_mapped(uint64_t base, size_t nbytes)")
    if mi < 0:
        raise SystemExit("no range_mapped")
    b = t.find("{", mi)
    depth = 0
    i = b
    while i < len(t):
        if t[i] == "{":
            depth += 1
        elif t[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
        i += 1
    else:
        raise SystemExit("brace range_mapped")
    helper = """

static int bfree_sigalt_onstack(uint64_t rsp)
{
    uint64_t sp;
    uint64_t top;

    if (g_sigalt_disable || g_sigalt_sp == 0 || g_sigalt_size == 0) {
        return 0;
    }
    sp = (uint64_t)(uintptr_t)g_sigalt_sp;
    top = sp + (uint64_t)g_sigalt_size;
    return rsp >= sp && rsp < top;
}
"""
    t = t[:end] + helper + t[end:]
    print("[ok] bfree_sigalt_onstack")
else:
    print("[skip] bfree_sigalt_onstack")

# --- sys_linux_sigaltstack before soft stubs ---
if "static long sys_linux_sigaltstack(" not in t:
    impl = """
static long sys_linux_sigaltstack(long uss, long uoss)
{
    /* Linux x86_64 stack_t: ss_sp, ss_flags, ss_size (with padding). */
    typedef struct {
        uint64_t ss_sp;
        int32_t ss_flags;
        int32_t pad;
        uint64_t ss_size;
    } bfree_stack_t;
    bfree_stack_t cur;
    bfree_stack_t neu;

    cur.ss_sp = (uint64_t)(uintptr_t)g_sigalt_sp;
    cur.ss_size = (uint64_t)g_sigalt_size;
    cur.pad = 0;
    if (g_sigalt_disable || g_sigalt_sp == 0) {
        cur.ss_flags = BFREE_SS_DISABLE;
    } else if (bfree_sigalt_onstack(g_bfree_user_sysret_rsp)) {
        cur.ss_flags = BFREE_SS_ONSTACK;
    } else {
        cur.ss_flags = 0;
    }

    if (uoss != 0) {
        if (!bfree_user_range_mapped((uint64_t)(uintptr_t)uoss, sizeof(bfree_stack_t))) {
            return -14;
        }
        *(bfree_stack_t *)(uintptr_t)uoss = cur;
    }

    if (uss == 0) {
        return 0;
    }
    if (!bfree_user_range_mapped((uint64_t)(uintptr_t)uss, sizeof(bfree_stack_t))) {
        return -14;
    }
    if (!g_sigalt_disable && g_sigalt_sp != 0 &&
        bfree_sigalt_onstack(g_bfree_user_sysret_rsp)) {
        return -1; /* EPERM */
    }
    neu = *(const bfree_stack_t *)(uintptr_t)uss;
    /* Soft: accept SS_AUTODISARM but do not auto-disarm (documented residual). */
    if ((neu.ss_flags & ~(BFREE_SS_ONSTACK | BFREE_SS_DISABLE | (int32_t)BFREE_SS_AUTODISARM)) != 0) {
        return -22; /* EINVAL */
    }
    if ((neu.ss_flags & BFREE_SS_DISABLE) != 0) {
        g_sigalt_sp = 0;
        g_sigalt_size = 0;
        g_sigalt_disable = 1;
        return 0;
    }
    if (neu.ss_size < (uint64_t)BFREE_MINSIGSTKSZ || neu.ss_sp == 0) {
        return -22;
    }
    if (!bfree_user_range_mapped(neu.ss_sp, (size_t)neu.ss_size)) {
        return -14;
    }
    g_sigalt_sp = (void *)(uintptr_t)neu.ss_sp;
    g_sigalt_size = (size_t)neu.ss_size;
    g_sigalt_disable = 0;
    return 0;
}

"""
    marker = "#ifndef BFREE_RESTORE_SOFT_STUBS"
    if marker not in t:
        raise SystemExit("no soft stubs marker")
    t = t.replace(marker, impl + marker, 1)
    print("[ok] sys_linux_sigaltstack")
else:
    print("[skip] sys_linux_sigaltstack")

# --- case 131 ---
if "case 131:" not in t:
    old = """    case 124: /* getsid */
        return sys_linux_getsid(arg1);
"""
    # may vary - find getuid area or clock
    if "case 124:" in t and "getsid" in t[t.find("case 124:"):t.find("case 124:")+80]:
        pos = t.find("case 124:")
        # insert after getsid case block
        line_end = t.find("\n", t.find("return", pos))
        t = t[: line_end + 1] + "    case 131: /* sigaltstack */\n        return sys_linux_sigaltstack(arg1, arg2);\n" + t[line_end + 1 :]
        print("[ok] case 131")
    else:
        # after case 121
        old2 = """    case 121: /* getpgid */
        return sys_linux_getpgid(arg1);
"""
        if old2 in t:
            t = t.replace(
                old2,
                old2
                + "    case 131: /* sigaltstack */\n"
                + "        return sys_linux_sigaltstack(arg1, arg2);\n",
                1,
            )
            print("[ok] case 131 (after 121)")
        else:
            raise SystemExit("no place for case 131")
else:
    print("[skip] case 131")

# --- SA_ONSTACK in deliver ---
old_rsp = """    rsp = g_bfree_user_sysret_rsp;
    if (rsp < (uint64_t)sizeof(bfree_rt_sigframe_t) + 16ULL) {
"""
new_rsp = """    rsp = g_bfree_user_sysret_rsp;
    /* H17: SA_ONSTACK → grow down from registered altstack top if not already on it. */
    if ((g_guest_sig_flags[sig] & BFREE_SA_ONSTACK) != 0UL &&
        !g_sigalt_disable && g_sigalt_sp != 0 && g_sigalt_size >= (size_t)BFREE_MINSIGSTKSZ &&
        !bfree_sigalt_onstack(rsp)) {
        rsp = ((uint64_t)(uintptr_t)g_sigalt_sp + (uint64_t)g_sigalt_size) & ~15ULL;
    }
    if (rsp < (uint64_t)sizeof(bfree_rt_sigframe_t) + 16ULL) {
"""
if "H17: SA_ONSTACK" not in t:
    if old_rsp not in t:
        raise SystemExit("deliver rsp anchor missing")
    t = t.replace(old_rsp, new_rsp, 1)
    print("[ok] SA_ONSTACK deliver")
else:
    print("[skip] SA_ONSTACK deliver")

# --- CLONE_THREAD gate ---
old_clone = """#define BFREE_LINUX_CLONE_VM     0x00000100
#define BFREE_LINUX_CLONE_VFORK  0x00004000

static long sys_linux_clone(long flags, long newsp, long ptid, long ctid, long tls)
{
    (void)newsp;
    (void)ptid;
    (void)ctid;
    (void)tls;
    /* Only the cooperative vfork-like subset is supported. Real fork/threads
     * need address-space copy and a scheduler. */
    if (((unsigned long)flags & (unsigned long)BFREE_LINUX_CLONE_VFORK) != 0UL) {
        return bfree_guest_fork_enter(0);
    }
    return -38; /* ENOSYS */
}
"""
new_clone = """#define BFREE_LINUX_CLONE_VM     0x00000100
#define BFREE_LINUX_CLONE_VFORK  0x00004000
#define BFREE_LINUX_CLONE_THREAD 0x00010000

static long sys_linux_clone(long flags, long newsp, long ptid, long ctid, long tls)
{
    unsigned long f = (unsigned long)flags;

    /* Serial shared-AS coop threads (H26). No preemptive parallel schedule. */
    if ((f & (unsigned long)BFREE_LINUX_CLONE_THREAD) != 0UL) {
        if ((f & (unsigned long)BFREE_LINUX_CLONE_VM) == 0UL) {
            return -38;
        }
        return bfree_guest_thread_clone(f, newsp, ptid, ctid, tls);
    }
    if ((f & (unsigned long)BFREE_LINUX_CLONE_VFORK) != 0UL) {
        return bfree_guest_fork_enter(0);
    }
    return -38; /* ENOSYS */
}
"""
if "BFREE_LINUX_CLONE_THREAD" not in t:
    if old_clone not in t:
        raise SystemExit("clone block missing")
    t = t.replace(old_clone, new_clone, 1)
    print("[ok] CLONE_THREAD gate")
else:
    print("[skip] CLONE_THREAD gate")

# H01 comment clarify residual
t = t.replace(
    "/* === H01 rt_sigframe / CATCH deliver (fpstate + nested CATCH still residual) === */",
    "/* === H01 rt_sigframe / CATCH deliver ===\n"
    " * Residuals (deferred→P3 polish): no glibc fpstate in frame; nested CATCH refused. */",
    1,
)

p.write_text(t, encoding="utf-8")
print("done")
