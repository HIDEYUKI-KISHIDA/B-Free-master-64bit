#!/usr/bin/env python3
"""Aggressive compile-fix for partially restored syscall.c."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "kernel" / "sysmain" / "syscall.c"
INET = ROOT / "tools" / "_recovered_syscall" / "inet.c"
text = PATH.read_text(encoding="utf-8", errors="replace")
orig = text

def ensure(snippet: str, after: str, label: str) -> None:
    global text
    if snippet.strip() in text:
        print("skip", label)
        return
    if after not in text:
        print("MISS anchor", label)
        return
    text = text.replace(after, after + "\n" + snippet + "\n", 1)
    print("OK", label)

# --- Missing globals / macros often dropped by partial replay ---
globals_block = r'''
/* --- restore glue: coop / inet / pgid (mobile restore pass) --- */
#ifndef BFREE_LINUX_AF_INET
#define BFREE_LINUX_AF_INET 2
#endif
#ifndef BFREE_INADDR_ANY
#define BFREE_INADDR_ANY 0U
#endif
#ifndef BFREE_INADDR_LOOPBACK
#define BFREE_INADDR_LOOPBACK 0x7f000001U
#endif
#ifndef BFREE_INET_SLOTS
#define BFREE_INET_SLOTS 8
#endif
#ifndef BFREE_INET_FD_BASE
#define BFREE_INET_FD_BASE 0x3B00  /* avoid clash with PTY 0x3A00 */
#endif
#ifndef BFREE_MSR_FS_BASE
#define BFREE_MSR_FS_BASE 0xC0000100ULL
#endif

#ifndef BFREE_UNIX_FD_BASE
#define BFREE_UNIX_FD_BASE 0x3900
#define BFREE_UNIX_SLOTS 8
#define BFREE_LINUX_AF_UNIX 1
#define BFREE_SYSRET_COOP_SWITCH ((long)-4092)
typedef struct {
    int used;
    int listening;
    int connected;
    int accept_rd;
    int pipe_magic;
    char path[96];
} bfree_unix_sock_t;
static bfree_unix_sock_t g_unix_socks[BFREE_UNIX_SLOTS];
#endif

typedef struct {
    int used;
    int listening;
    int connected;
    int bound;
    uint32_t addr;
    uint16_t port;
    int accept_rd;
    int pipe_magic;
} bfree_inet_sock_t;
static bfree_inet_sock_t g_inet_socks[BFREE_INET_SLOTS];

static int g_coop_side;
static int g_coop_child_blocked;
static int g_coop_parent_started;
static int g_coop_parent_resume_mode;
static int g_coop_child_resume_mode;
static uint64_t g_coop_parent_resume_rax;
static uint64_t g_coop_child_resume_rax;
static uint64_t g_coop_parent_rcx;
static long g_coop_cur_nr;
static int g_guest_pgid;
static int g_guest_sys_trace;

static uint64_t bfree_rdmsr64(uint32_t msr);
static void bfree_wrmsr64(uint32_t msr, uint64_t val);
static int bfree_user_ptr_mapped(long ptr);
'''

# Insert after post-wipe stubs or after includes-ish early globals
anchor = "uint64_t g_bfree_sig_saved_r15;"
if "g_inet_socks[" not in text or text.count("g_inet_socks") < 2:
    # only inject typedef if missing declaration of array
    if "static bfree_inet_sock_t g_inet_socks" not in text:
        if anchor in text:
            text = text.replace(anchor, anchor + "\n" + globals_block, 1)
            print("OK injected restore globals")
        else:
            # fallback near top after first uint64_t g_bfree
            m = re.search(r"uint64_t g_bfree_sysret_exec_cr3;", text)
            if m:
                text = text[: m.end()] + "\n" + globals_block + text[m.end() :]
                print("OK injected restore globals (fallback)")
            else:
                print("MISS globals anchor")
else:
    print("skip inet socks already dense")

# Soft stubs for missing helpers referenced by dispatch
stubs = r'''
static long sys_linux_fchown(long fd, long uid, long gid) { (void)fd;(void)uid;(void)gid; return 0; }
static long sys_linux_chown(long path, long uid, long gid) { (void)path;(void)uid;(void)gid; return 0; }
static long sys_linux_dup3(long a, long b, long c) { (void)c; return sys_linux_dup2(a, b); }
static long sys_linux_flock(long fd, long op) { (void)fd;(void)op; return 0; }
static long sys_linux_fsync(long fd) { (void)fd; return 0; }
static long sys_linux_alarm(long sec) { (void)sec; return 0; }
static long sys_linux_getitimer(long which, long curr) { (void)which;(void)curr; return 0; }
static long sys_linux_setitimer(long which, long newv, long oldv) { (void)which;(void)newv;(void)oldv; return 0; }
static long sys_linux_getsid(long pid) { (void)pid; return g_guest_pgid ? g_guest_pgid : 1; }
static long sys_linux_set_robust_list(long head, long len) { (void)head;(void)len; return 0; }
static long sys_linux_rt_sigpending(long set) { (void)set; return 0; }
static long sys_linux_rt_sigreturn(void) { return 0; }
static long sys_linux_clock_getres_linux(long clk, long tp) { return sys_linux_clock_getres ? 0 : 0; (void)clk;(void)tp; return 0; }
static long sys_linux_mmap(long a, long b, long c, long d, long e, long f) {
    (void)d;(void)e;(void)f;
    return sys_mmap ? sys_mmap(a,b,c) : -38;
}
'''

# Safer stubs without bogus refs
stubs = r'''
#ifndef BFREE_RESTORE_SOFT_STUBS
#define BFREE_RESTORE_SOFT_STUBS 1
static long sys_linux_fchown(long fd, long uid, long gid) { (void)fd;(void)uid;(void)gid; return 0; }
static long sys_linux_chown(long path, long uid, long gid) { (void)path;(void)uid;(void)gid; return 0; }
static long sys_linux_flock(long fd, long op) { (void)fd;(void)op; return 0; }
static long sys_linux_fsync(long fd) { (void)fd; return 0; }
static long sys_linux_alarm(long sec) { (void)sec; return 0; }
static long sys_linux_getitimer(long which, long curr) { (void)which;(void)curr; return 0; }
static long sys_linux_setitimer(long which, long newv, long oldv) { (void)which;(void)newv;(void)oldv; return 0; }
static long sys_linux_getsid(long pid) { (void)pid; return 1; }
static long sys_linux_set_robust_list(long head, long len) { (void)head;(void)len; return 0; }
static long sys_linux_rt_sigpending(long set) { (void)set; return 0; }
static long sys_linux_rt_sigreturn(void) { return 0; }
static long sys_linux_clock_getres_linux(long clk, long tp) { (void)clk;(void)tp; return 0; }
static int bfree_pty_slot_from_fd(int fd) { (void)fd; return -1; }
static unsigned initrd_presence_mask(void) { return 0; }
#endif
'''

if "BFREE_RESTORE_SOFT_STUBS" not in text:
    # place before linux dispatch if possible
    needle = "static long bfree_dispatch_linux_guest_syscall"
    if needle in text:
        text = text.replace(needle, stubs + "\n" + needle, 1)
        print("OK soft stubs")
    else:
        text = stubs + "\n" + text
        print("OK soft stubs at top")

# Deduplicate consecutive identical case labels by commenting later ones — hard.
# Instead remove duplicate `case N:` lines that immediately reappear with same N in switch — skip for now.

# Fix fork_enter arity: if calls pass args but def is void, strip args at call sites
text2 = re.sub(
    r"bfree_guest_fork_enter\([^)]+\)",
    "bfree_guest_fork_enter()",
    text,
)
if text2 != text:
    text = text2
    print("OK normalized fork_enter() calls")

# Remove duplicate function definitions for thread_* (keep first)
for name in (
    "bfree_guest_thread_init",
    "bfree_guest_thread_clone",
    "bfree_guest_thread_exit",
    "bfree_guest_thread_save_parent_ctx",
):
    pat = re.compile(
        rf"(static (?:void|long) {name}\s*\([^;]*?\)\s*\{{.*?\n\}})\n",
        re.S,
    )
    ms = list(pat.finditer(text))
    if len(ms) > 1:
        for m in reversed(ms[1:]):
            text = text[: m.start()] + f"/* dup removed: {name} */\n" + text[m.end() :]
        print("OK dedup", name, "kept 1 of", len(ms))

PATH.write_text(text, encoding="utf-8", newline="\n")
print("wrote", PATH.stat().st_size, "delta", PATH.stat().st_size - len(orig.encode()))
