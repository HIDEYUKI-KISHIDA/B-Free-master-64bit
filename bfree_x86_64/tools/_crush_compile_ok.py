#!/usr/bin/env python3
"""Crush remaining restore compile errors in syscall.c to COMPILE_OK."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "kernel" / "sysmain" / "syscall.c"
text = PATH.read_text(encoding="utf-8", errors="replace")
orig = text


def once(marker: str, patch_fn) -> None:
    global text
    if marker in text:
        print("skip", marker)
        return
    text = patch_fn(text)
    if marker not in text and "OK" not in marker:
        # patch_fn should leave a recognizable marker; tolerate soft stubs
        pass
    print("did", marker)


# ---------------------------------------------------------------------------
# 1) Early MSR + forwards + missing globals BEFORE first use (thread_clone)
# ---------------------------------------------------------------------------
EARLY = r'''
/* === restore compile glue (pre-wipe resume) === */
#ifndef BFREE_MSR_FS_BASE
#define BFREE_MSR_FS_BASE 0xC0000100ULL
#endif
#ifndef BFREE_SIG_DFL
#define BFREE_SIG_DFL   0
#define BFREE_SIG_IGN   1
#define BFREE_SIG_CATCH 2
#define BFREE_NSIG      64
#endif
#ifndef BFREE_UNIX_SLOTS
#define BFREE_LINUX_AF_UNIX 1
#define BFREE_UNIX_SLOTS 8
#define BFREE_UNIX_FD_BASE 0x3900
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

static int bfree_user_ptr_mapped(long ptr);
static void bfree_wrmsr64(uint32_t msr, uint64_t val);
static uint64_t bfree_rdmsr64(uint32_t msr);
static int bfree_pty_slot_from_fd(int fd);
static int bfree_inet_from_fd(int fd);
static void bfree_inet_sock_release(int resolved);
static uint16_t bfree_inet_ntohs(uint16_t x);
static uint32_t bfree_inet_ntohl(uint32_t x);
static int bfree_unix_from_fd(int fd);
static long bfree_coop_yield_to_parent_done(long ret);
static long bfree_coop_yield_to_child_done(long ret);
static void bfree_guest_sig_raise(int sig);
static int bfree_guest_sig_take_eintr(void);
static long bfree_guest_sig_try_deliver(long ret);
static long bfree_guest_exit_from_fork_signal(int sig);

#ifndef BFREE_RESTORE_COOP_GLOBALS
#define BFREE_RESTORE_COOP_GLOBALS 1
static int g_coop_side;
static int g_coop_child_blocked;
static int g_coop_parent_started;
static int g_coop_session = -1;
static int g_guest_waitid_active;
static long g_guest_waitid_infop;
static long g_guest_wait_status_ptr;
static int g_guest_tty_pgrp = 1;
static int g_guest_sid = 1;
static uint8_t g_guest_sig_disp[BFREE_NSIG];
#endif
/* === end restore compile glue === */

'''

if "restore compile glue" not in text:
    anchor = "static void bfree_guest_thread_init(void)"
    if anchor not in text:
        raise SystemExit("FAIL: thread_init anchor")
    text = text.replace(anchor, EARLY + anchor, 1)
    print("OK early glue")
else:
    print("skip early glue")

# ---------------------------------------------------------------------------
# 2) Soft stub bodies (before dispatch) — only if not already defined
# ---------------------------------------------------------------------------
STUBS = r'''
/* === restore soft bodies (compile-only; H02/H01 replace later) === */
#ifndef BFREE_RESTORE_SOFT_BODIES
#define BFREE_RESTORE_SOFT_BODIES 1
static int bfree_unix_from_fd(int fd)
{
    int idx;
    if (fd < (int)BFREE_UNIX_FD_BASE || fd >= (int)BFREE_UNIX_FD_BASE + BFREE_UNIX_SLOTS) {
        return -1;
    }
    idx = fd - (int)BFREE_UNIX_FD_BASE;
    return g_unix_socks[idx].used ? idx : -1;
}
static int bfree_inet_from_fd(int fd)
{
    int idx;
    if (fd < (int)BFREE_INET_FD_BASE || fd >= (int)BFREE_INET_FD_BASE + BFREE_INET_SLOTS) {
        return -1;
    }
    idx = fd - (int)BFREE_INET_FD_BASE;
    return g_inet_socks[idx].used ? idx : -1;
}
static void bfree_inet_sock_release(int resolved)
{
    int iidx = bfree_inet_from_fd(resolved);
    if (iidx < 0) {
        return;
    }
    g_inet_socks[iidx].used = 0;
    g_inet_socks[iidx].listening = 0;
    g_inet_socks[iidx].connected = 0;
    g_inet_socks[iidx].bound = 0;
    g_inet_socks[iidx].accept_rd = -1;
    g_inet_socks[iidx].pipe_magic = -1;
}
static uint16_t bfree_inet_ntohs(uint16_t x)
{
    return (uint16_t)(((x & 0xffU) << 8) | ((x >> 8) & 0xffU));
}
static uint32_t bfree_inet_ntohl(uint32_t x)
{
    return ((x & 0xffU) << 24) | ((x & 0xff00U) << 8) |
           ((x >> 8) & 0xff00U) | ((x >> 24) & 0xffU);
}
static long bfree_coop_yield_to_parent_done(long ret)
{
    /* Soft: full AS-copy yield resumes in H02. */
    return ret;
}
static long bfree_coop_yield_to_child_done(long ret)
{
    return ret;
}
static void bfree_guest_sig_raise(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return;
    }
    if (sig != 9 && g_guest_sig_disp[sig] == BFREE_SIG_IGN) {
        return;
    }
    (void)sig;
}
static int bfree_guest_sig_take_eintr(void)
{
    return 0;
}
static long bfree_guest_sig_try_deliver(long ret)
{
    return ret;
}
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

'''

if "BFREE_RESTORE_SOFT_BODIES" not in text:
    # Insert just before soft stubs / dispatch
    anchor = "#ifndef BFREE_RESTORE_SOFT_STUBS"
    if anchor in text:
        text = text.replace(anchor, STUBS + anchor, 1)
        print("OK soft bodies before soft stubs")
    else:
        anchor2 = "static long bfree_dispatch_linux_guest_syscall"
        if anchor2 not in text:
            raise SystemExit("FAIL: dispatch anchor")
        text = text.replace(anchor2, STUBS + anchor2, 1)
        print("OK soft bodies before dispatch")
else:
    print("skip soft bodies")

# ---------------------------------------------------------------------------
# 3) Fix chown arity (calls pass dirfd, path, uid, gid)
# ---------------------------------------------------------------------------
old_chown = "static long sys_linux_chown(long path, long uid, long gid) { (void)path;(void)uid;(void)gid; return 0; }"
new_chown = "static long sys_linux_chown(long dirfd, long path, long uid, long gid) { (void)dirfd;(void)path;(void)uid;(void)gid; return 0; }"
if old_chown in text:
    text = text.replace(old_chown, new_chown, 1)
    print("OK chown 4-arg")
elif "static long sys_linux_chown(long dirfd, long path, long uid, long gid)" in text:
    print("skip chown already 4-arg")
else:
    # try regex
    text2, n = re.subn(
        r"static long sys_linux_chown\(long path, long uid, long gid\)",
        "static long sys_linux_chown(long dirfd, long path, long uid, long gid)",
        text,
        count=1,
    )
    if n:
        text = text2
        # also fix body void casts if needed
        text = text.replace(
            "static long sys_linux_chown(long dirfd, long path, long uid, long gid) { (void)path;(void)uid;(void)gid; return 0; }",
            "static long sys_linux_chown(long dirfd, long path, long uid, long gid) { (void)dirfd;(void)path;(void)uid;(void)gid; return 0; }",
            1,
        )
        print("OK chown regex")
    else:
        print("WARN chown not found")

# ---------------------------------------------------------------------------
# 4) Ensure pty_slot stub exists early enough — keep late stub but it's OK
#    if forward decl exists. Remove duplicate late if we already define early.
# ---------------------------------------------------------------------------
# If late stub exists and forward exists, fine. If soft stubs define pty_slot
# at end only, forward early is enough.

# Move pty_slot definition into soft bodies if not present as a real body before use
if "static int bfree_pty_slot_from_fd(int fd) { (void)fd; return -1; }" in text:
    # ensure we don't have TWO bodies — soft stubs section already has one at end
    # Count definitions with body
    bodies = list(re.finditer(r"static int bfree_pty_slot_from_fd\s*\([^)]*\)\s*\{", text))
    if len(bodies) > 1:
        # keep first body, comment later ones
        print(f"NOTE pty_slot bodies={len(bodies)}")

# ---------------------------------------------------------------------------
# 5) Drop redundant late forward decls that redeclare after implicit? already fixed by early.
#    Also remove duplicate `#ifndef BFREE_MSR_FS_BASE` blocks are fine.
# ---------------------------------------------------------------------------

# If soft stubs still have 3-arg chown after our replace failed partially — handled above.

PATH.write_text(text, encoding="utf-8", newline="\n")
print("wrote", PATH, "bytes", PATH.stat().st_size, "delta", len(text) - len(orig))
