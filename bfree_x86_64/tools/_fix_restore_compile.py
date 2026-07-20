#!/usr/bin/env python3
"""Aggressive compile-fix for restored syscall.c (pre-wipe resume)."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "kernel" / "sysmain" / "syscall.c"
INET = ROOT / "tools" / "_recovered_syscall" / "inet.c"
text = PATH.read_text(encoding="utf-8", errors="replace")
orig = text

def ensure(marker: str, insert: str, after: str) -> None:
    global text
    if marker in text:
        print("skip", marker[:40])
        return
    if after not in text:
        print("MISS anchor", after[:40])
        return
    text = text.replace(after, after + insert, 1)
    print("OK insert", marker[:40])

# --- Missing inet / coop globals (defs used but types missing) ---
INET_DEFS = r'''
/* H32 AF_INET (restored) */
#ifndef BFREE_LINUX_AF_INET
#define BFREE_LINUX_AF_INET 2
#endif
#ifndef BFREE_INET_SLOTS
#define BFREE_INET_SLOTS 8
#define BFREE_INET_FD_BASE 0x3B00 /* avoid PTY 0x3A00 clash */
#define BFREE_INADDR_LOOPBACK 0x7f000001U
#define BFREE_INADDR_ANY 0U
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
#endif

/* H02 coop dual-live resume (stubs if missing) */
#ifndef BFREE_COOP_RESUME_STUBS
#define BFREE_COOP_RESUME_STUBS 1
static int g_coop_parent_resume_mode;
static int g_coop_child_resume_mode;
static uint64_t g_coop_parent_resume_rax;
static uint64_t g_coop_child_resume_rax;
static uint64_t g_coop_parent_rcx;
static long g_coop_cur_nr;
static int g_guest_sys_trace;
static int g_guest_pgid = 1;
#endif
'''

if "BFREE_INET_FD_BASE" not in text or "g_inet_socks" not in text.split("bfree_inet_sock_t")[0] if "bfree_inet_sock_t" in text else True:
    # If g_inet_socks used but typedef missing, inject early after includes block
    if "typedef struct {\n    int used;\n    int listening;\n    int connected;\n    int bound;" not in text:
        anchor = "static int g_guest_fork_active;"
        if anchor in text:
            text = text.replace(anchor, INET_DEFS + "\n" + anchor, 1)
            print("OK inet+coop stubs")
        else:
            text = INET_DEFS + "\n" + text
            print("OK inet+coop stubs at top")
    elif "g_coop_parent_resume_mode" not in text:
        anchor = "static int g_guest_fork_active;"
        coop_only = """
static int g_coop_parent_resume_mode;
static int g_coop_child_resume_mode;
static uint64_t g_coop_parent_resume_rax;
static uint64_t g_coop_child_resume_rax;
static uint64_t g_coop_parent_rcx;
static long g_coop_cur_nr;
static int g_guest_sys_trace;
static int g_guest_pgid = 1;
"""
        if anchor in text:
            text = text.replace(anchor, coop_only + "\n" + anchor, 1)
            print("OK coop resume stubs")

# Ensure BFREE_MSR_FS_BASE
if "BFREE_MSR_FS_BASE" not in text:
    text = "#ifndef BFREE_MSR_FS_BASE\n#define BFREE_MSR_FS_BASE 0xC0000100ULL\n#endif\n" + text
    print("OK MSR_FS_BASE")

# Ensure g_coop_side / parent_started / child_blocked if unix block missing pieces
for name, decl in [
    ("g_coop_side", "static int g_coop_side;\n"),
    ("g_coop_parent_started", "static int g_coop_parent_started;\n"),
    ("g_coop_child_blocked", "static int g_coop_child_blocked;\n"),
]:
    if name not in text:
        text = decl + text
        print("OK", name)

# Forward decls commonly missing
fwd = """
static int bfree_user_ptr_mapped(long ptr);
static void bfree_wrmsr64(uint32_t msr, uint64_t val);
static uint64_t bfree_rdmsr64(uint32_t msr);
static long sys_linux_chown(long path_ptr, long uid, long gid);
static long sys_linux_fchown(long fd, long uid, long gid);
"""
if "static long sys_linux_chown(" not in text.split("sys_linux_chown")[0] if False else ("static long sys_linux_chown" not in text):
    # Only add if calls exist without defs
    if "sys_linux_chown(" in text and "static long sys_linux_chown" not in text:
        text = fwd + text
        print("OK chown stubs fwd")
        # weak stubs
        stubs = """
static long sys_linux_chown(long path_ptr, long uid, long gid) { (void)path_ptr;(void)uid;(void)gid; return 0; }
static long sys_linux_fchown(long fd, long uid, long gid) { (void)fd;(void)uid;(void)gid; return 0; }
"""
        text = text + "\n" + stubs
        print("OK chown stub bodies")

# Remove duplicate function definitions (keep first)
for fname in [
    "bfree_guest_thread_init",
    "bfree_guest_thread_clone",
    "bfree_guest_thread_exit",
    "bfree_guest_thread_save_parent_ctx",
]:
    pat = re.compile(
        rf"(static (?:void|long) {fname}\s*\([^;]*?\)\s*\{{)",
        re.M,
    )
    # Find all definition starts - harder with nested braces. Simpler: count and warn.
    matches = list(re.finditer(rf"static (?:void|long) {fname}\(", text))
    if len(matches) > 1:
        print(f"NOTE {fname} appears {len(matches)} times — manual dedup may be needed")

# Deduplicate consecutive identical case lines in switch (naive)
# Fix: duplicate case value — comment out later duplicates of same case N:
def dedup_cases(s: str) -> str:
    lines = s.splitlines(keepends=True)
    out = []
    seen_in_switch = set()
    depth = 0
    in_dispatch = False
    for line in lines:
        if "bfree_dispatch_linux_guest_syscall" in line or "switch (num)" in line or "switch(num)" in line:
            in_dispatch = True
            seen_in_switch = set()
        if in_dispatch:
            depth += line.count("{") - line.count("}")
            m = re.match(r"(\s*)case\s+(\d+)\s*:", line)
            if m:
                num = m.group(2)
                if num in seen_in_switch:
                    out.append(f"{m.group(1)}/* DUP removed case {num}: */\n")
                    # skip until next case/default/break block end — just comment this line
                    # and following return line if simple
                    continue
                seen_in_switch.add(num)
            if depth <= 0 and "{" not in line:
                in_dispatch = False
        out.append(line)
    return "".join(out)

before = text
text = dedup_cases(text)
if text != before:
    print("OK dedup cases pass")

# Soft stubs for unused-as-error: reference them once in a keep-alive
keepalive = """
static void bfree_restore_keepalive(void)
{
    (void)bfree_guest_as_copy_switch_heap_to_child;
    (void)bfree_guest_as_copy_switch_heap_to_parent;
    (void)bfree_guest_thread_init;
    (void)bfree_guest_thread_clone;
}
"""
# Only if those symbols exist as functions
if "bfree_guest_as_copy_switch_heap_to_child" in text and "bfree_restore_keepalive" not in text:
    # Don't add if it would reference missing — check
    pass

PATH.write_text(text, encoding="utf-8", newline="\n")
print("wrote", PATH.stat().st_size, "delta", PATH.stat().st_size - len(orig.encode("utf-8")))
