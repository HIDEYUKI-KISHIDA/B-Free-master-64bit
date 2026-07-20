#!/usr/bin/env python3
"""Apply AF_UNIX + coop yields with correct C declaration order for HEAD layout."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "kernel" / "sysmain" / "syscall.c"
BLOCK = (ROOT / "tools" / "_unix_coop_block.c").read_text(encoding="utf-8")
text = PATH.read_text(encoding="utf-8", errors="replace")

if "BFREE_UNIX_FD_BASE" in text:
    print("unix/coop already present")
else:
    fwd = """
/* Forward decls: unix/coop bodies live after FD table (H14/H02) */
static void bfree_guest_alarm_poll(void);
static int bfree_guest_sig_take_eintr(void);
static void bfree_coop_fd_snap_init(void);
static long bfree_coop_yield_to_parent(void);
static long bfree_coop_yield_to_child(void);
static int bfree_guest_fd_publish(int target);
static long sys_linux_read(long fd, long buf, long count);
static long sys_linux_write(long fd, long buf, long count);

"""
    anchor = "static long bfree_guest_fork_enter(void)\n"
    if anchor not in text:
        raise SystemExit("FAIL: fork_enter anchor")
    text = text.replace(anchor, fwd + anchor, 1)
    print("OK forward decls")

    stubs = """
/* Soft stubs until H04/H13 signal pending is restored */
static void bfree_guest_alarm_poll(void) {}
static int bfree_guest_sig_take_eintr(void) { return 0; }

"""
    # Place bodies after FD table helper that unix block needs
    needle = "static void bfree_guest_fd_ensure_init(void)\n{\n"
    # Find end of bfree_guest_fd_ensure_init function — insert AFTER next function's start is hard;
    # instead insert immediately BEFORE bfree_guest_fd_ensure_init, but FD array must already exist.
    # Array is just above ensure_init — insert after ensure_init's closing brace of first occurrence.
    idx = text.find("static void bfree_guest_fd_ensure_init(void)")
    if idx < 0:
        raise SystemExit("FAIL: fd_ensure_init")
    # Find the function body end: first "}\n\nstatic" after idx
    end = text.find("\n}\n\n", idx)
    if end < 0:
        raise SystemExit("FAIL: fd_ensure_init end")
    end = end + 3  # include "}\n\n"
    text = text[:end] + stubs + BLOCK + "\n" + text[end:]
    print("OK inserted unix/coop after fd_ensure_init")

n2 = (
    "    g_guest_fork_active = 1;\n"
    "    g_guest_fork_status_ready = 0;\n"
    "    return 0; /* cooperative: continue as child; parent resumes on child exit */"
)
r2 = (
    "    g_guest_fork_active = 1;\n"
    "    g_guest_fork_status_ready = 0;\n"
    "    bfree_coop_fd_snap_init();\n"
    "    return 0; /* cooperative: continue as child; parent resumes on child exit */"
)
if "bfree_coop_fd_snap_init();" not in text and n2 in text:
    text = text.replace(n2, r2, 1)
    print("OK fork snap")

old_wait = """            while (ps->len == 0 && ps->wr_open > 0) {
                __asm__ volatile("sti; hlt" ::: "memory");
            }"""
new_wait = """            while (ps->len == 0 && ps->wr_open > 0) {
                if (g_guest_fork_active && g_coop_side == 1) {
                    return bfree_coop_yield_to_parent();
                }
                bfree_guest_alarm_poll();
                {
                    int er = bfree_guest_sig_take_eintr();
                    if (er < 0) {
                        return er;
                    }
                }
                __asm__ volatile("sti; hlt" ::: "memory");
            }"""
if "return bfree_coop_yield_to_parent();" not in text and old_wait in text:
    text = text.replace(old_wait, new_wait, 1)
    print("OK pipe read yield")

needle_wr = """        for (i = 0; i < n; ++i) {
            ps->buf[ps->len + i] = src[i];
        }
        ps->len += n;
        return (long)n;
    }
    if (bfree_guest_is_eventfd(fd)) {"""
repl_wr = """        for (i = 0; i < n; ++i) {
            ps->buf[ps->len + i] = src[i];
        }
        ps->len += n;
        /* B-Free: coop yield after pipe write */
        if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked && n > 0) {
            return bfree_coop_yield_to_child();
        }
        return (long)n;
    }
    if (bfree_guest_is_eventfd(fd)) {"""
if "coop yield after pipe write" not in text and needle_wr in text:
    text = text.replace(needle_wr, repl_wr, 1)
    print("OK pipe write yield")

if "case 41: /* socket */" not in text:
    n5 = "    case 53:\n        return sys_linux_socketpair(arg1, arg2, arg3, arg4);"
    r5 = """    case 41: /* socket */
        return sys_linux_socket(arg1, arg2, arg3);
    case 49: /* bind */
        return sys_linux_bind(arg1, arg2, arg3);
    case 50: /* listen */
        return sys_linux_listen(arg1, arg2);
    case 43: /* accept */
        return sys_linux_accept(arg1, arg2, arg3);
    case 42: /* connect */
        return sys_linux_connect(arg1, arg2, arg3);
    case 44: /* sendto */
        return sys_linux_sendto(arg1, arg2, arg3, arg4, arg5, 0);
    case 45: /* recvfrom */
        return sys_linux_recvfrom(arg1, arg2, arg3, arg4, arg5, 0);
    case 53:
        return sys_linux_socketpair(arg1, arg2, arg3, arg4);"""
    if n5 in text:
        text = text.replace(n5, r5, 1)
        print("OK unix dispatch")
    else:
        print("WARN dispatch needle missing")

PATH.write_text(text, encoding="utf-8", newline="\n")
print("wrote", PATH, "bytes", PATH.stat().st_size)
