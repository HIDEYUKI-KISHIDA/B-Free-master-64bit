#!/usr/bin/env python3
"""Structural repairs for replay-damaged syscall.c so it can compile."""
from __future__ import annotations

import re
from pathlib import Path

PATH = Path(__file__).resolve().parents[2] / "kernel" / "sysmain" / "syscall.c"
text = PATH.read_text(encoding="utf-8", errors="replace")
orig = text

def once_replace(old: str, new: str, label: str) -> None:
    global text
    if old not in text:
        print("MISS", label)
        return
    text = text.replace(old, new, 1)
    print("OK", label)

# --- 1) Remove duplicate bfree_siginfo_wait_t (keep first) ---
siginfo_pat = re.compile(
    r"typedef struct \{\n"
    r"    int si_signo;\n"
    r"    int si_errno;\n"
    r"    int si_code;\n"
    r"    int __pad0;\n"
    r"    int si_pid;\n"
    r"    unsigned int si_uid;\n"
    r"    int si_status;\n"
    r"    long si_utime;\n"
    r"    long si_stime;\n"
    r"\} bfree_siginfo_wait_t;\n",
    re.M,
)
ms = list(siginfo_pat.finditer(text))
print("siginfo typedefs", len(ms))
if len(ms) > 1:
    # remove all but first, also nearby duplicate define/forward blocks between them if identical
    for m in reversed(ms[1:]):
        text = text[: m.start()] + text[m.end() :]
    print("OK removed duplicate siginfo typedefs")

# --- 2) Remove FIRST (simple) vfile typedef; keep richer one ---
simple_vfile = """typedef struct {
    int used;
    int is_symlink; /* data[] holds the link target instead of file contents */
    char name[48];
    unsigned char data[BFREE_GUEST_VFILE_SIZE];
    size_t len;
    size_t pos;
} bfree_guest_vfile_t;
"""
if simple_vfile in text and "int orphaned;" in text:
    text = text.replace(simple_vfile, "/* simple bfree_guest_vfile_t removed; richer typedef kept below */\n", 1)
    print("OK removed simple vfile typedef")
else:
    print("MISS simple vfile typedef")

# --- 3) Fix orphaned proc_maps string literals ---
orphan = '''    /* QV4 stackProperties() parses the first region containing stackAddr. */
    "08000000-10000000 rw-p 00000000 00:00 0                  [stack]\\n"
    "00100000-02000000 rw-p 00000000 00:00 0                  [stack]\\n"
    "08000000-14000000 r-xp 00000000 00:00 0                  /desktop\\n"
    "19000000-21000000 rw-p 00000000 00:00 0                  [heap]\\n";
    "05000000-05380000 r-xp 00000000 00:00 0                  /busybox.elf\\n"
    "05380000-05400000 rw-p 00000000 00:00 0                  /busybox.elf\\n"
    "03c00000-2a000000 rw-p 00000000 00:00 0                  [heap]\\n"
    "01380000-01400000 rw-p 00000000 00:00 0                  [stack]\\n";
static const char *g_guest_proc_maps = g_guest_proc_maps_desktop;
'''
fixed_maps = '''static const char g_guest_proc_maps_desktop[] =
    /* QV4 stackProperties() parses the first region containing stackAddr. */
    "08000000-10000000 rw-p 00000000 00:00 0                  [stack]\\n"
    "00100000-02000000 rw-p 00000000 00:00 0                  [stack]\\n"
    "08000000-14000000 r-xp 00000000 00:00 0                  /desktop\\n"
    "19000000-21000000 rw-p 00000000 00:00 0                  [heap]\\n";
static const char g_guest_proc_maps_busybox[] =
    "05000000-05380000 r-xp 00000000 00:00 0                  /busybox.elf\\n"
    "05380000-05400000 rw-p 00000000 00:00 0                  /busybox.elf\\n"
    "03c00000-2a000000 rw-p 00000000 00:00 0                  [heap]\\n"
    "01380000-01400000 rw-p 00000000 00:00 0                  [stack]\\n";
static const char *g_guest_proc_maps = g_guest_proc_maps_desktop;
'''
if orphan in text:
    text = text.replace(orphan, fixed_maps, 1)
    print("OK fixed orphaned proc_maps")
else:
    # try more flexible
    if '"08000000-10000000 rw-p' in text and "static const char g_guest_proc_maps_desktop" not in text:
        text2 = re.sub(
            r"    /\* QV4 stackProperties\(\) parses the first region containing stackAddr\. \*/\n"
            r"    \"08000000-10000000[^\"]+\"\n"
            r"    \"00100000-02000000[^\"]+\"\n"
            r"    \"08000000-14000000[^\"]+\"\n"
            r"    \"19000000-21000000[^\"]+\";\n"
            r"    \"05000000-05380000[^\"]+\"\n"
            r"    \"05380000-05400000[^\"]+\"\n"
            r"    \"03c00000-2a000000[^\"]+\"\n"
            r"    \"01380000-01400000[^\"]+\";\n"
            r"static const char \*g_guest_proc_maps = g_guest_proc_maps_desktop;\n",
            fixed_maps,
            text,
            count=1,
        )
        if text2 != text:
            text = text2
            print("OK fixed orphaned proc_maps (regex)")
        else:
            print("MISS orphaned proc_maps pattern")
    else:
        print("SKIP proc_maps (already ok or absent)")

# --- 4) Fix missing size_t i in exit_from_fork ---
once_replace(
    """static long bfree_guest_exit_from_fork(long status)
{
    int *cleartid;
    /* Use enter-time AS-copy flag — NOT has_private_as (vfork+exec sets that). */
    int as_copy = g_guest_fork_was_as_copy;

    bfree_process_exit_child((int)status);
""",
    """static long bfree_guest_exit_from_fork(long status)
{
    int *cleartid;
    size_t i;
    /* Use enter-time AS-copy flag — NOT has_private_as (vfork+exec sets that). */
    int as_copy = g_guest_fork_was_as_copy;

    bfree_process_exit_child((int)status);
""",
    "exit_from_fork size_t i",
)

# --- 5) Early decls / stubs after coop forward decls ---
anchor = """static void bfree_coop_fd_switch_to(int side);
static int g_coop_side;
static int g_coop_child_blocked;
"""
early = """static void bfree_coop_fd_switch_to(int side);
static int g_coop_side;
static int g_coop_child_blocked;
static int g_coop_parent_started;
static int g_coop_session = -1;
static long g_guest_wait_status_ptr;
static int g_guest_waitid_active;
static long g_guest_waitid_infop;
static uint32_t g_guest_uid;
static uint32_t g_guest_gid;
static uint32_t g_guest_euid;
static uint32_t g_guest_egid;
static uint64_t g_guest_sig_pending;
static uint64_t g_guest_sig_mask;
/* Callee-saved snapshot for BFREE_SYSRET_SIGNAL (syscall_entry.S). */
uint64_t g_bfree_sig_saved_rbx;
uint64_t g_bfree_sig_saved_rbp;
uint64_t g_bfree_sig_saved_r12;
uint64_t g_bfree_sig_saved_r13;
uint64_t g_bfree_sig_saved_r14;
uint64_t g_bfree_sig_saved_r15;
uint64_t g_bfree_sig_saved_rdx;

static void bfree_guest_cwd_copy(char *dst, size_t dst_sz, const char *src);
static void bfree_guest_load_heap_cwd(const char *cwd_src, uint64_t heap_src, uint64_t brk_src);
static void bfree_guest_park_heap_side(int child_side);
static void bfree_guest_as_copy_switch_heap_to_child(void);
static void bfree_guest_as_copy_switch_heap_to_parent(void);
static void bfree_guest_restore_parent_isol(int as_copy);
static void bfree_coop_publish_parent_resume(void);
static void bfree_coop_publish_child_resume(void);
static long bfree_coop_yield_to_parent_done(long ret);
static long bfree_coop_yield_to_child_done(long ret);
static void bfree_coop_session_park_globals(int sess);
static void bfree_coop_sessions_init(void);
static long bfree_guest_exit_from_fork_signal(int sig);
static void bfree_inet_sock_release(int resolved);
static int bfree_guest_is_vfile_fd(int fd);
static int bfree_guest_alias_add(int vnode, const char *name);
static int bfree_guest_vfile_unlink_name(const char *name);
static int bfree_linux_path_is_dot_or_slash(const char *path);
static long bfree_linux_stat_fill(long statbuf, uint32_t mode, uint64_t size, uint64_t ino);
static long sys_linux_syscall_dispatch_exit_reenter(long status);
#ifndef BFREE_GUEST_HOME_DIR_FD
#define BFREE_GUEST_HOME_DIR_FD 0x3720
#endif
"""

if "static uint64_t g_guest_sig_pending;" not in text and anchor in text:
    # If g_coop_parent_started already declared early, still inject the rest carefully
    text = text.replace(anchor, early, 1)
    print("OK early decls injected")
elif "static uint64_t g_guest_sig_pending;" in text:
    print("SKIP early decls (sig_pending already present)")
else:
    print("MISS early decl anchor")

# Remove late duplicate `static int g_coop_parent_started;` if early now exists
if text.count("static int g_coop_parent_started;") > 1:
    # keep first
    first = text.find("static int g_coop_parent_started;")
    second = text.find("static int g_coop_parent_started;", first + 1)
    if second > 0:
        text = text[:second] + text[second + len("static int g_coop_parent_started;\n") :]
        print("OK removed late dup g_coop_parent_started")

# --- 6) Inject as_copy heap helpers if missing ---
if "bfree_guest_as_copy_switch_heap_to_child" not in text or "static void bfree_guest_as_copy_switch_heap_to_child(void)\n{" not in text:
    helpers = r'''
static void bfree_guest_cwd_copy(char *dst, size_t dst_sz, const char *src)
{
    size_t i;

    for (i = 0; i < dst_sz; ++i) {
        dst[i] = src[i];
        if (src[i] == '\0') {
            break;
        }
    }
    if (dst_sz > 0U) {
        dst[dst_sz - 1U] = '\0';
    }
}

static void bfree_guest_load_heap_cwd(const char *cwd_src, uint64_t heap_src, uint64_t brk_src)
{
    bfree_guest_cwd_copy(g_guest_cwd, sizeof(g_guest_cwd), cwd_src);
    g_guest_heap_next = heap_src;
    g_guest_brk = brk_src;
}

static void bfree_guest_park_heap_side(int child_side)
{
    if (child_side) {
        g_guest_child_parked_heap_next = g_guest_heap_next;
        g_guest_child_parked_brk = g_guest_brk;
        bfree_guest_cwd_copy(g_guest_child_parked_cwd, sizeof(g_guest_child_parked_cwd),
                             g_guest_cwd);
        g_guest_child_parked_heap_valid = 1;
    } else {
        g_guest_parent_parked_heap_next = g_guest_heap_next;
        g_guest_parent_parked_brk = g_guest_brk;
        bfree_guest_cwd_copy(g_guest_parent_parked_cwd, sizeof(g_guest_parent_parked_cwd),
                             g_guest_cwd);
        g_guest_parent_parked_heap_valid = 1;
    }
}

static void bfree_guest_as_copy_switch_heap_to_child(void)
{
    bfree_guest_park_heap_side(0);
    if (g_guest_child_parked_heap_valid) {
        bfree_guest_load_heap_cwd(g_guest_child_parked_cwd,
                                  g_guest_child_parked_heap_next,
                                  g_guest_child_parked_brk);
    } else {
        bfree_guest_load_heap_cwd(g_guest_fork_saved_cwd,
                                  g_guest_fork_saved_heap_next,
                                  g_guest_fork_saved_brk);
    }
}

static void bfree_guest_as_copy_switch_heap_to_parent(void)
{
    bfree_guest_park_heap_side(1);
    if (g_guest_parent_parked_heap_valid) {
        bfree_guest_load_heap_cwd(g_guest_parent_parked_cwd,
                                  g_guest_parent_parked_heap_next,
                                  g_guest_parent_parked_brk);
    }
}

static void bfree_guest_restore_parent_isol(int as_copy)
{
    if (as_copy && g_guest_parent_parked_heap_valid) {
        bfree_guest_load_heap_cwd(g_guest_parent_parked_cwd,
                                  g_guest_parent_parked_heap_next,
                                  g_guest_parent_parked_brk);
    } else {
        bfree_guest_load_heap_cwd(g_guest_fork_saved_cwd,
                                  g_guest_fork_saved_heap_next,
                                  g_guest_fork_saved_brk);
    }
    g_guest_parent_parked_heap_valid = 0;
    g_guest_child_parked_heap_valid = 0;
}

/* Dual-live session stubs (full H02 not recovered). */
static void bfree_coop_session_park_globals(int sess)
{
    (void)sess;
}

static void bfree_coop_sessions_init(void)
{
    g_coop_session = -1;
}

'''
    # Insert after parked cwd globals
    needle = "static char g_guest_child_parked_cwd[256];\n"
    if needle in text:
        text = text.replace(needle, needle + helpers, 1)
        print("OK injected as_copy heap helpers + session stubs")
    else:
        print("MISS parked cwd needle for helpers")

# --- 7) Stub publish_parent_resume if only declared ---
if "void bfree_coop_publish_parent_resume(void)\n{" not in text and "bfree_coop_publish_parent_resume(void)" in text:
    # add weak implementation near as_switch
    stub = """
static void bfree_coop_publish_parent_resume(void)
{
    g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
    g_bfree_sysret_exec_rcx = g_bfree_fork_saved_rcx;
    g_bfree_sysret_exec_r11 = g_bfree_fork_saved_r11;
    if (g_bfree_fork_parent_ret == 0) {
        g_bfree_fork_parent_ret = (uint64_t)(long)g_guest_fork_pid;
    }
}

static void bfree_coop_publish_child_resume(void)
{
    g_bfree_sysret_exec_rsp = g_coop_child_rsp;
    g_bfree_sysret_exec_rcx = g_coop_child_rcx;
    g_bfree_sysret_exec_r11 = g_coop_child_r11;
}
"""
    # Prefer insert before first bfree_coop_as_switch_to definition
    m = re.search(r"static void bfree_coop_as_switch_to\(int side\)\n\{", text)
    if m:
        text = text[: m.start()] + stub + "\n" + text[m.start() :]
        print("OK stub publish_*_resume")
    else:
        print("MISS as_switch for publish stubs")

# --- 8) Ensure g_fd_snap_* exist early if used before unix block ---
if "static int g_fd_snap_parent[" not in text and "g_fd_snap_parent" in text:
    # will be in unix block - add forward? better add arrays near coop
    snap = """
#ifndef BFREE_GUEST_FD_TABLE_SIZE
#define BFREE_GUEST_FD_TABLE_SIZE 64
#endif
static int g_fd_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
"""
    needle = "static int g_coop_child_blocked;\n"
    # already replaced - find g_coop_session
    if "static int g_fd_snap_parent[" not in text:
        n2 = "static int g_coop_session = -1;\n"
        if n2 in text:
            text = text.replace(n2, n2 + snap, 1)
            print("OK early fd snap arrays")
        else:
            print("MISS fd snap insert point")
elif text.count("static int g_fd_snap_parent[") > 1:
    # remove later duplicate definitions of snap arrays - keep first
    print("NOTE multiple g_fd_snap_parent defs", text.count("static int g_fd_snap_parent["))

# --- 9) kernel_page_table extern ---
if "extern page_table_t kernel_page_table" not in text and "kernel_page_table" in text:
    text = text.replace(
        '#include "../../userland/libc/bfree_epoll.h"\n',
        '#include "../../userland/libc/bfree_epoll.h"\n\nextern page_table_t kernel_page_table;\n',
        1,
    )
    print("OK extern kernel_page_table")

# --- 10) soft stubs for rare missing helpers used before defs ---
extra_stubs = ""
if "bfree_linux_user_pt" in text and "bfree_linux_user_pt(" in text:
    # check for definition
    if not re.search(r"\bbfree_linux_user_pt\s*\([^;]*\)\s*\{", text):
        extra_stubs += """
static page_table_t *bfree_linux_user_pt(void)
{
    if (knl_current_task != 0 && knl_current_task->page_table_base != 0) {
        return (page_table_t *)knl_current_task->page_table_base;
    }
    return &kernel_page_table;
}

static void *bfree_linux_user_kva(uint64_t uaddr)
{
    (void)uaddr;
    return (void *)(uintptr_t)uaddr; /* identity user map in B-Free guest */
}
"""
        print("will add linux_user_pt stubs")

if "sys_linux_syscall_dispatch_exit_reenter" in text and not re.search(
    r"sys_linux_syscall_dispatch_exit_reenter\s*\([^;]*\)\s*\{", text
):
    extra_stubs += """
static long sys_linux_syscall_dispatch_exit_reenter(long status)
{
    return bfree_guest_exit_from_fork(status);
}
"""
    print("will add exit_reenter stub")

if extra_stubs:
    # place after restore_parent_isol if present
    mark = "static void bfree_guest_restore_parent_isol(int as_copy)\n"
    idx = text.find(mark)
    if idx >= 0:
        # find end of that function
        brace = text.find("{", idx)
        depth = 0
        j = brace
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    j += 1
                    text = text[:j] + "\n" + extra_stubs + text[j:]
                    print("OK extra stubs inserted")
                    break
            j += 1
    else:
        text = text.replace(
            "extern page_table_t kernel_page_table;\n",
            "extern page_table_t kernel_page_table;\n" + extra_stubs,
            1,
        )
        print("OK extra stubs at top")

if text != orig:
    PATH.write_text(text, encoding="utf-8")
    print("wrote", PATH, "delta", len(text) - len(orig))
else:
    print("no changes")
