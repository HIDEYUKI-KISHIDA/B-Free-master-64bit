#!/usr/bin/env python3
"""H02: wire AS-copy pipe concurrency — coop CR3 switch, fork enter, yield_done."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "kernel" / "sysmain" / "syscall.c"
text = PATH.read_text(encoding="utf-8", errors="replace")
orig = text

MARKER = "BFREE_H02_AS_COPY_WIRED"


def must(cond: bool, msg: str) -> None:
    if not cond:
        raise SystemExit("FAIL: " + msg)


if MARKER in text:
    print("skip: already wired")
else:
    # --- 1) Ensure COOP_SWITCH magic ---
    if "BFREE_SYSRET_COOP_SWITCH" not in text:
        needle = "#define BFREE_SYSRET_FORK_PARENT   ((long)-4093)\n"
        must(needle in text, "FORK_PARENT define")
        text = text.replace(
            needle,
            needle + "#define BFREE_SYSRET_COOP_SWITCH   ((long)-4092)\n",
            1,
        )
        print("OK COOP_SWITCH define")

    # --- 2) Parent park slots next to existing g_coop_parent_rcx ---
    if "g_coop_parent_rsp" not in text:
        needle = "static uint64_t g_coop_parent_rcx;\n"
        must(needle in text, "g_coop_parent_rcx")
        text = text.replace(
            needle,
            needle
            + "static uint64_t g_coop_parent_r11;\n"
            + "static uint64_t g_coop_parent_rsp;\n"
            + "static uint64_t g_coop_parent_rbx;\n"
            + "static uint64_t g_coop_parent_rbp;\n"
            + "static uint64_t g_coop_parent_r12;\n"
            + "static uint64_t g_coop_parent_r13;\n"
            + "static uint64_t g_coop_parent_r14;\n"
            + "static uint64_t g_coop_parent_r15;\n"
            + "static uint64_t g_coop_parent_rdx;\n",
            1,
        )
        print("OK parent park regs")

    # --- 3) Child coop context regs (if missing) ---
    if "g_coop_child_rcx" not in text:
        needle = "static int g_coop_parent_started;\n"
        # may be under BFREE_RESTORE_COOP_GLOBALS
        if "static int g_coop_parent_started;" in text:
            text = text.replace(
                "static int g_coop_parent_started;\n",
                "static int g_coop_parent_started;\n"
                "static uint64_t g_coop_child_rcx, g_coop_child_r11, g_coop_child_rsp;\n"
                "static uint64_t g_coop_child_rbx, g_coop_child_rbp, g_coop_child_r12;\n"
                "static uint64_t g_coop_child_r13, g_coop_child_r14, g_coop_child_r15, g_coop_child_rdx;\n",
                1,
            )
            print("OK child coop regs")
        else:
            raise SystemExit("FAIL: g_coop_parent_started")

    # --- 4) Forward decls near early glue ---
    if "bfree_coop_as_switch_to" not in text.split("bfree_coop_yield_to_parent_done")[0]:
        # add forwards after yield_done forward
        fwd = (
            "static long bfree_coop_yield_to_parent_done(long ret);\n"
            "static long bfree_coop_yield_to_child_done(long ret);\n"
        )
        extra = (
            "static void bfree_coop_fd_snap_init(void);\n"
            "static void bfree_coop_fd_switch_to(int side);\n"
            "static void bfree_coop_as_switch_to(int side);\n"
            "static long bfree_coop_yield_to_parent(void);\n"
            "static long bfree_coop_yield_to_child(void);\n"
            "static long bfree_guest_fork_enter(int copy_as);\n"
        )
        if fwd in text:
            text = text.replace(fwd, fwd + extra, 1)
            print("OK coop forwards")
        else:
            # early glue has yield_done forwards separately
            needle = "static long bfree_coop_yield_to_child_done(long ret);\n"
            must(needle in text, "yield_done fwd")
            text = text.replace(needle, needle + extra, 1)
            print("OK coop forwards (alt)")

    # --- 5) Insert coop body block before soft bodies; replace soft yield_done ---
    COOP_BODY = r'''
/* === H02 AS-copy / coop pipe concurrency === */
#ifndef BFREE_H02_AS_COPY_WIRED
#define BFREE_H02_AS_COPY_WIRED 1

static void bfree_coop_fd_snap_init(void)
{
    int i;
    bfree_guest_fd_ensure_init();
    for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
        g_fd_snap_parent[i] = g_guest_fd_target[i];
        g_fd_snap_child[i] = g_guest_fd_target[i];
    }
    g_coop_side = 1;
    g_coop_child_blocked = 0;
    g_coop_parent_started = 0;
}

static void bfree_coop_fd_switch_to(int side)
{
    int i;
    if (side == g_coop_side) {
        return;
    }
    if (g_coop_side == 1) {
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_fd_snap_child[i] = g_guest_fd_target[i];
        }
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_guest_fd_target[i] = g_fd_snap_parent[i];
        }
    } else {
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_fd_snap_parent[i] = g_guest_fd_target[i];
        }
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_guest_fd_target[i] = g_fd_snap_child[i];
        }
    }
    g_coop_side = side;
}

static void bfree_coop_save_child_user(void)
{
    g_coop_child_rcx = g_bfree_user_sysret_rcx;
    g_coop_child_r11 = g_bfree_user_sysret_r11;
    g_coop_child_rsp = g_bfree_user_sysret_rsp;
    g_coop_child_rbx = g_bfree_user_sysret_rbx;
    g_coop_child_rbp = g_bfree_user_sysret_rbp;
    g_coop_child_r12 = g_bfree_user_sysret_r12;
    g_coop_child_r13 = g_bfree_user_sysret_r13;
    g_coop_child_r14 = g_bfree_user_sysret_r14;
    g_coop_child_r15 = g_bfree_user_sysret_r15;
    g_coop_child_rdx = g_bfree_user_sysret_rdx;
}

static void bfree_coop_save_parent_user(void)
{
    g_coop_parent_rcx = g_bfree_user_sysret_rcx;
    g_coop_parent_r11 = g_bfree_user_sysret_r11;
    g_coop_parent_rsp = g_bfree_user_sysret_rsp;
    g_coop_parent_rbx = g_bfree_user_sysret_rbx;
    g_coop_parent_rbp = g_bfree_user_sysret_rbp;
    g_coop_parent_r12 = g_bfree_user_sysret_r12;
    g_coop_parent_r13 = g_bfree_user_sysret_r13;
    g_coop_parent_r14 = g_bfree_user_sysret_r14;
    g_coop_parent_r15 = g_bfree_user_sysret_r15;
    g_coop_parent_rdx = g_bfree_user_sysret_rdx;
}

/* Stage parent into fork_saved_* for FORK_PARENT/COOP (-4092/-4093). */
static void bfree_coop_publish_parent_resume(void)
{
    g_bfree_fork_saved_rbx = g_coop_parent_rbx;
    g_bfree_fork_saved_rbp = g_coop_parent_rbp;
    g_bfree_fork_saved_r12 = g_coop_parent_r12;
    g_bfree_fork_saved_r13 = g_coop_parent_r13;
    g_bfree_fork_saved_r14 = g_coop_parent_r14;
    g_bfree_fork_saved_r15 = g_coop_parent_r15;
    g_bfree_fork_saved_rdx = g_coop_parent_rdx;
    g_bfree_fork_saved_rcx = g_coop_parent_rcx;
    g_bfree_fork_saved_r11 = g_coop_parent_r11;
    g_bfree_fork_saved_rsp = g_coop_parent_rsp;
    g_bfree_sysret_exec_rsp = g_coop_parent_rsp;
    g_bfree_sysret_exec_rcx = g_coop_parent_rcx;
    g_bfree_sysret_exec_r11 = g_coop_parent_r11;
}

/* Stage child into fork_saved_* (entry.S COOP uses fork_saved RIP/RSP). */
static void bfree_coop_publish_child_resume(void)
{
    g_bfree_fork_saved_rbx = g_coop_child_rbx;
    g_bfree_fork_saved_rbp = g_coop_child_rbp;
    g_bfree_fork_saved_r12 = g_coop_child_r12;
    g_bfree_fork_saved_r13 = g_coop_child_r13;
    g_bfree_fork_saved_r14 = g_coop_child_r14;
    g_bfree_fork_saved_r15 = g_coop_child_r15;
    g_bfree_fork_saved_rdx = g_coop_child_rdx;
    g_bfree_fork_saved_rcx = g_coop_child_rcx;
    g_bfree_fork_saved_r11 = g_coop_child_r11;
    g_bfree_fork_saved_rsp = g_coop_child_rsp;
    g_bfree_sysret_exec_rsp = g_coop_child_rsp;
    g_bfree_sysret_exec_rcx = g_coop_child_rcx;
    g_bfree_sysret_exec_r11 = g_coop_child_r11;
}

/* H02: flip CR3 with logical parent/child side. FORK_PARENT must not use exec_cr3. */
static void bfree_coop_as_switch_to(int side)
{
    page_table_t *pt;

    if (!knl_current_task) {
        return;
    }
    if (!bfree_process_child_has_private_as()) {
        return;
    }
    pt = (side == 0) ? bfree_process_parent_pt() : bfree_process_child_pt();
    if (!pt) {
        return;
    }
    if ((page_table_t *)knl_current_task->page_table_base == pt) {
        return;
    }
    knl_current_task->page_table_base = pt;
    __asm__ volatile("mov %0, %%cr3" :: "r"(pt) : "memory");
    /* Never leave a stale exec_cr3 for FORK_PARENT/COOP resume. */
    g_bfree_sysret_exec_cr3 = 0;
}

static void bfree_coop_arm_parent_resume(void)
{
    if (!g_coop_parent_started) {
        g_coop_parent_started = 1;
        g_bfree_fork_parent_ret = (uint64_t)(long)g_guest_fork_pid;
        return;
    }
    if (g_coop_parent_resume_mode == 2) {
        g_bfree_fork_saved_rcx = g_coop_parent_rcx;
        g_bfree_fork_parent_ret = g_coop_parent_resume_rax;
        g_coop_parent_resume_mode = 0;
        return;
    }
    if (g_coop_parent_rcx >= 2) {
        g_bfree_fork_saved_rcx = g_coop_parent_rcx - 2;
    }
    if (g_coop_parent_resume_mode == 1) {
        g_bfree_fork_parent_ret = g_coop_parent_resume_rax;
        g_coop_parent_resume_mode = 0;
    } else {
        g_bfree_fork_parent_ret = 0;
    }
}

static void bfree_coop_arm_child_resume(void)
{
    if (g_coop_child_resume_mode == 1 || g_coop_child_resume_mode == 2) {
        g_bfree_fork_parent_ret = g_coop_child_resume_rax;
        g_coop_child_resume_mode = 0;
    } else {
        g_bfree_fork_parent_ret = 0;
    }
}

static long bfree_coop_yield_to_parent(void)
{
    bfree_coop_save_child_user();
    if (g_coop_child_rcx >= 2) {
        g_coop_child_rcx -= 2;
    }
    g_coop_child_resume_rax = (uint64_t)g_coop_cur_nr;
    g_coop_child_resume_mode = 1;
    g_coop_child_blocked = 1;
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_parent();
    }
    bfree_coop_fd_switch_to(0);
    bfree_coop_as_switch_to(0);
    if (g_guest_fork_saved_fsbase != 0) {
        if (knl_current_task != 0) {
            knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
    }
    bfree_coop_publish_parent_resume();
    bfree_coop_arm_parent_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

static long bfree_coop_yield_to_child(void)
{
    bfree_coop_save_parent_user();
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_child();
    }
    g_coop_parent_resume_rax = (uint64_t)g_coop_cur_nr;
    g_coop_parent_resume_mode = 1;
    bfree_coop_fd_switch_to(1);
    if (g_guest_fork_saved_fsbase != 0) {
        if (knl_current_task != 0) {
            knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
    }
    bfree_coop_as_switch_to(1);
    g_coop_child_blocked = 0;
    bfree_coop_publish_child_resume();
    bfree_coop_arm_child_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

static long bfree_coop_yield_to_parent_done(long ret)
{
    bfree_coop_save_child_user();
    g_coop_child_resume_rax = (uint64_t)(long)ret;
    g_coop_child_resume_mode = 2;
    g_coop_child_blocked = 1;
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_parent();
    }
    bfree_coop_fd_switch_to(0);
    bfree_coop_as_switch_to(0);
    bfree_coop_publish_parent_resume();
    bfree_coop_arm_parent_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

static long bfree_coop_yield_to_child_done(long ret)
{
    bfree_coop_save_parent_user();
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_child();
    }
    g_coop_parent_resume_rax = (uint64_t)(long)ret;
    g_coop_parent_resume_mode = 2;
    bfree_coop_fd_switch_to(1);
    bfree_coop_as_switch_to(1);
    g_coop_child_blocked = 0;
    bfree_coop_publish_child_resume();
    bfree_coop_arm_child_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

#endif /* BFREE_H02_AS_COPY_WIRED */
'''

    # Place before soft bodies; strip soft yield_done from soft bodies
    soft = "/* === restore soft bodies (compile-only; H02/H01 replace later) === */"
    must(soft in text, "soft bodies marker")
    text = text.replace(soft, COOP_BODY + "\n" + soft, 1)
    print("OK coop body inserted")

    # Remove soft yield_done stubs to avoid redefinition
    text2, n = re.subn(
        r"static long bfree_coop_yield_to_parent_done\(long ret\)\s*\{\s*/\* Soft:.*?\n\s*return ret;\s*\}\s*"
        r"static long bfree_coop_yield_to_child_done\(long ret\)\s*\{\s*return ret;\s*\}\s*",
        "/* H02: yield_*_done provided above */\n",
        text,
        count=1,
        flags=re.S,
    )
    if n:
        text = text2
        print("OK removed soft yield_done")
    else:
        # try simpler
        text2, n = re.subn(
            r"static long bfree_coop_yield_to_parent_done\(long ret\)\s*\{[^}]*\}\s*"
            r"static long bfree_coop_yield_to_child_done\(long ret\)\s*\{[^}]*\}",
            "/* H02: yield_*_done provided above */",
            text,
            count=1,
        )
        if n:
            text = text2
            print("OK removed soft yield_done (simple)")
        else:
            print("WARN could not remove soft yield_done — may duplicate")

    # --- 6) Upgrade fork_enter(void) -> fork_enter(int copy_as) ---
    old_fe = """static long bfree_guest_fork_enter()
{
    size_t i;
    int child_pid = 0;
    long rc;

    /* Snapshot parent return point before child syscalls clobber user_sysret_*. */
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
    g_guest_fork_saved_fsbase = bfree_rdmsr64((uint32_t)BFREE_MSR_FS_BASE);
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
    }
    for (i = 0; i < sizeof(g_guest_fork_saved_cwd); ++i) {
        g_guest_fork_saved_cwd[i] = g_guest_cwd[i];
        if (g_guest_cwd[i] == '\\0') {
            break;
        }
    }
    g_guest_fork_saved_cwd[sizeof(g_guest_fork_saved_cwd) - 1U] = '\\0';
    g_guest_fork_saved_heap_next = g_guest_heap_next;
    g_guest_fork_saved_brk = g_guest_brk;
    (void)bfree_guest_vfork_stack_snapshot(g_bfree_fork_saved_rsp);

    rc = bfree_process_vfork_enter(&child_pid);
    if (rc < 0) {
        return rc;
    }
    g_guest_fork_pid = child_pid;
    g_guest_fork_active = 1;
    g_guest_fork_status_ready = 0;
    return 0; /* cooperative: continue as child; parent resumes on child exit */
}
"""
    # Fix escape - the file has real null chars as '\0' in source as two chars
    old_fe = old_fe.replace("\\0", "\0")  # wrong - in C source it's backslash-zero
    # Better: regex replace the function

    new_fe = r'''static long bfree_guest_fork_enter(int copy_as)
{
    size_t i;
    int child_pid = 0;
    long rc;

    /* Snapshot parent return point before child syscalls clobber user_sysret_*. */
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
    /* Seed parent park so later COOP publish works after AS-copy parent-first. */
    g_coop_parent_rcx = g_bfree_user_sysret_rcx;
    g_coop_parent_r11 = g_bfree_user_sysret_r11;
    g_coop_parent_rsp = g_bfree_user_sysret_rsp;
    g_coop_parent_rbx = g_bfree_user_sysret_rbx;
    g_coop_parent_rbp = g_bfree_user_sysret_rbp;
    g_coop_parent_r12 = g_bfree_user_sysret_r12;
    g_coop_parent_r13 = g_bfree_user_sysret_r13;
    g_coop_parent_r14 = g_bfree_user_sysret_r14;
    g_coop_parent_r15 = g_bfree_user_sysret_r15;
    g_coop_parent_rdx = g_bfree_user_sysret_rdx;
    g_guest_fork_saved_fsbase = bfree_rdmsr64((uint32_t)BFREE_MSR_FS_BASE);
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
    }
    for (i = 0; i < sizeof(g_guest_fork_saved_cwd); ++i) {
        g_guest_fork_saved_cwd[i] = g_guest_cwd[i];
        if (g_guest_cwd[i] == '\0') {
            break;
        }
    }
    g_guest_fork_saved_cwd[sizeof(g_guest_fork_saved_cwd) - 1U] = '\0';
    g_guest_fork_saved_heap_next = g_guest_heap_next;
    g_guest_fork_saved_brk = g_guest_brk;
    if (!copy_as) {
        (void)bfree_guest_vfork_stack_snapshot(g_bfree_fork_saved_rsp);
    }

    if (copy_as) {
        /* EAGAIN if another live AS-copy/vfork child already occupies a slot. */
        rc = bfree_process_fork_enter(&child_pid);
    } else {
        rc = bfree_process_vfork_enter(&child_pid);
    }
    if (rc < 0) {
        return rc;
    }
    g_guest_fork_pid = child_pid;
    g_guest_fork_active = 1;
    g_guest_fork_status_ready = 0;
    g_guest_fork_was_as_copy = copy_as ? 1 : 0;
    g_guest_parent_parked_heap_valid = 0;
    g_guest_child_parked_heap_valid = 0;
    bfree_coop_fd_snap_init();
    if (copy_as) {
        /*
         * AS-copy: return to parent immediately (parent-first). Child parked
         * until parent blocks on stdin/wait/pipe and yields. CR3 flipped in C;
         * FORK_PARENT/COOP must not consume g_bfree_sysret_exec_cr3.
         */
        bfree_coop_save_child_user();
        g_coop_child_blocked = 1;
        bfree_coop_fd_switch_to(0);
        bfree_coop_as_switch_to(0);
        g_coop_parent_started = 1;
        g_bfree_sysret_exec_cr3 = 0;
        bfree_coop_publish_parent_resume();
        g_bfree_fork_parent_ret = (uint64_t)(long)g_guest_fork_pid;
        return BFREE_SYSRET_COOP_SWITCH;
    }
    return 0; /* vfork: continue as child; parent resumes on child exit */
}
'''

    m = re.search(
        r"static long bfree_guest_fork_enter\s*\(\s*\)\s*\{",
        text,
    )
    must(m is not None, "fork_enter() def")
    # replace until matching close of function - find by brace count
    start = m.start()
    i = m.end() - 1
    depth = 0
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
        i += 1
    else:
        raise SystemExit("FAIL: fork_enter brace")
    text = text[:start] + new_fe + text[end:]
    print("OK fork_enter(copy_as)")

    # --- 7) Update callers ---
    # case 57/58
    old_cases = """    case 57: /* fork — no address-space copy yet */
        return -38; /* ENOSYS */
    case 58: /* vfork */
        return bfree_guest_fork_enter();
"""
    new_cases = """    case 57: /* fork — cooperative eager AS copy (H02) */
        return bfree_guest_fork_enter(1);
    case 58: /* vfork — shared AS until exec/exit */
        return bfree_guest_fork_enter(0);
"""
    if old_cases in text:
        text = text.replace(old_cases, new_cases, 1)
        print("OK case 57/58")
    else:
        # try looser
        text2, n = re.subn(
            r"case 57:.*?case 58:.*?return bfree_guest_fork_enter\(\);",
            "case 57: /* fork — cooperative eager AS copy (H02) */\n"
            "        return bfree_guest_fork_enter(1);\n"
            "    case 58: /* vfork — shared AS until exec/exit */\n"
            "        return bfree_guest_fork_enter(0);",
            text,
            count=1,
            flags=re.S,
        )
        if n:
            text = text2
            print("OK case 57/58 regex")
        else:
            print("WARN case 57/58 not patched")

    # Other fork_enter() calls
    text2, n = re.subn(r"bfree_guest_fork_enter\(\)", "bfree_guest_fork_enter(0)", text)
    # but also wrongly replaced the definition? definition is (int copy_as) now
    # and case 57 already has (1). Count remaining bare calls - we may have doubled (0)(0)
    text = text2
    text = text.replace("bfree_guest_fork_enter(0)(0)", "bfree_guest_fork_enter(0)")
    text = text.replace("bfree_guest_fork_enter(1)(0)", "bfree_guest_fork_enter(1)")
    print("OK fork_enter call sites normalized, n=", n)

    # --- 8) exit_from_fork: respect was_as_copy ---
    old_exit_snip = """    bfree_process_exit_child((int)status);
    g_guest_fork_active = 0;
    g_guest_fork_status = (int)(status & 0xff);
    g_guest_fork_status_ready = 1;
    /* Shared fd table: a pipeline child may leave stdin/stdout wired to a
     * pipe. Restore the shell's console before the parent resumes. */
    bfree_guest_stdio_heal_pipes();
    for (i = 0; i < sizeof(g_guest_cwd); ++i) {
        g_guest_cwd[i] = g_guest_fork_saved_cwd[i];
        if (g_guest_fork_saved_cwd[i] == '\\0') {
            break;
        }
    }
    g_guest_cwd[sizeof(g_guest_cwd) - 1U] = '\\0';
    g_guest_heap_next = g_guest_fork_saved_heap_next;
    g_guest_brk = g_guest_fork_saved_brk;
    bfree_guest_vfork_stack_restore();
"""
    # Use regex for exit path
    exit_pat = re.compile(
        r"(static long bfree_guest_exit_from_fork\(long status\)\s*\{\s*"
        r"int \*cleartid;\s*"
        r"size_t i;\s*\n)",
        re.M,
    )
    m = exit_pat.search(text)
    if m and "int as_copy = g_guest_fork_was_as_copy;" not in text[m.start() : m.start() + 400]:
        text = exit_pat.sub(
            r"\1    int as_copy = g_guest_fork_was_as_copy;\n\n",
            text,
            count=1,
        )
        print("OK exit as_copy local")

    # Replace heap/stack restore block
    restore_old = """    bfree_guest_stdio_heal_pipes();
    for (i = 0; i < sizeof(g_guest_cwd); ++i) {
        g_guest_cwd[i] = g_guest_fork_saved_cwd[i];
        if (g_guest_fork_saved_cwd[i] == '\\0') {
            break;
        }
    }
    g_guest_cwd[sizeof(g_guest_cwd) - 1U] = '\\0';
    g_guest_heap_next = g_guest_fork_saved_heap_next;
    g_guest_brk = g_guest_fork_saved_brk;
    bfree_guest_vfork_stack_restore();
"""
    # actual file uses '\0' as two-char sequence in source
    restore_old = """    bfree_guest_stdio_heal_pipes();
    for (i = 0; i < sizeof(g_guest_cwd); ++i) {
        g_guest_cwd[i] = g_guest_fork_saved_cwd[i];
        if (g_guest_fork_saved_cwd[i] == '\\0') {
            break;
        }
    }
    g_guest_cwd[sizeof(g_guest_cwd) - 1U] = '\\0';
    g_guest_heap_next = g_guest_fork_saved_heap_next;
    g_guest_brk = g_guest_fork_saved_brk;
    bfree_guest_vfork_stack_restore();
"""
    # Let's do it with raw from file content
    restore_old = (
        "    bfree_guest_stdio_heal_pipes();\n"
        "    for (i = 0; i < sizeof(g_guest_cwd); ++i) {\n"
        "        g_guest_cwd[i] = g_guest_fork_saved_cwd[i];\n"
        "        if (g_guest_fork_saved_cwd[i] == '\\0') {\n"
        "            break;\n"
        "        }\n"
        "    }\n"
        "    g_guest_cwd[sizeof(g_guest_cwd) - 1U] = '\\0';\n"
        "    g_guest_heap_next = g_guest_fork_saved_heap_next;\n"
        "    g_guest_brk = g_guest_fork_saved_brk;\n"
        "    bfree_guest_vfork_stack_restore();\n"
    )
    restore_new = (
        "    bfree_guest_sig_raise(17);\n"
        "    bfree_coop_fd_switch_to(0);\n"
        "    g_coop_child_blocked = 0;\n"
        "    bfree_guest_stdio_heal_pipes();\n"
        "    /* AS-copy: parent kept running — do not rewind heap/stack. */\n"
        "    if (!as_copy) {\n"
        "        for (i = 0; i < sizeof(g_guest_cwd); ++i) {\n"
        "            g_guest_cwd[i] = g_guest_fork_saved_cwd[i];\n"
        "            if (g_guest_fork_saved_cwd[i] == '\\0') {\n"
        "                break;\n"
        "            }\n"
        "        }\n"
        "        g_guest_cwd[sizeof(g_guest_cwd) - 1U] = '\\0';\n"
        "        g_guest_heap_next = g_guest_fork_saved_heap_next;\n"
        "        g_guest_brk = g_guest_fork_saved_brk;\n"
        "        bfree_guest_vfork_stack_restore();\n"
        "    } else {\n"
        "        bfree_guest_restore_parent_isol(1);\n"
        "        bfree_coop_as_switch_to(0);\n"
        "    }\n"
        "    g_guest_fork_was_as_copy = 0;\n"
    )
    if restore_old in text:
        text = text.replace(restore_old, restore_new, 1)
        print("OK exit_from_fork as_copy path")
    else:
        print("WARN exit restore block not found exact; trying regex")
        text2, n = re.subn(
            r"    bfree_guest_stdio_heal_pipes\(\);\n"
            r"    for \(i = 0; i < sizeof\(g_guest_cwd\); \+\+i\) \{\n"
            r"        g_guest_cwd\[i\] = g_guest_fork_saved_cwd\[i\];\n"
            r"        if \(g_guest_fork_saved_cwd\[i\] == '\\0'\) \{\n"
            r"            break;\n"
            r"        \}\n"
            r"    \}\n"
            r"    g_guest_cwd\[sizeof\(g_guest_cwd\) - 1U\] = '\\0';\n"
            r"    g_guest_heap_next = g_guest_fork_saved_heap_next;\n"
            r"    g_guest_brk = g_guest_fork_saved_brk;\n"
            r"    bfree_guest_vfork_stack_restore\(\);\n",
            restore_new,
            text,
            count=1,
        )
        if n:
            text = text2
            print("OK exit_from_fork regex")
        else:
            print("WARN exit_from_fork not patched")

    # Ensure g_guest_fork_active clear also clears was_as before? already set 0 above

    # --- 9) waitpid: yield to AS-copy child when blocking ---
    old_wait = """static long sys_linux_waitpid(long pid, long status_ptr, long options)
{
    int status = 0;
    long rc;

    rc = bfree_process_wait4(pid,
        (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) ? &status : 0,
        (int)options);
    if (rc > 0) {
        g_guest_fork_status_ready = 0;
        if (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) {
            *(int *)(uintptr_t)status_ptr = status;
        }
    }
    return rc;
}
"""
    new_wait = """static long sys_linux_waitpid(long pid, long status_ptr, long options)
{
    int status = 0;
    long rc;

    for (;;) {
        rc = bfree_process_wait4(pid,
            (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) ? &status : 0,
            (int)options);
        if (rc > 0) {
            g_guest_fork_status_ready = 0;
            if (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) {
                *(int *)(uintptr_t)status_ptr = status;
            }
            return rc;
        }
        if (rc < 0) {
            return rc; /* ECHILD */
        }
        if (((unsigned)options & 1U) != 0U) { /* WNOHANG */
            return 0;
        }
        /* H02: live AS-copy child — schedule it instead of sti;hlt forever. */
        if (g_guest_fork_was_as_copy || g_coop_parent_started) {
            if (bfree_process_child_active() || g_guest_fork_active) {
                g_guest_fork_active = 1;
                g_guest_wait_status_ptr = status_ptr;
                g_guest_waitid_active = 0;
                return bfree_coop_yield_to_child();
            }
        }
        if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked) {
            g_guest_wait_status_ptr = status_ptr;
            return bfree_coop_yield_to_child();
        }
        {
            int er = bfree_guest_sig_take_eintr();
            if (er < 0) {
                return er;
            }
        }
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}
"""
    if old_wait in text:
        text = text.replace(old_wait, new_wait, 1)
        print("OK waitpid yield")
    else:
        print("WARN waitpid not patched exact")

    # --- 10) Pipe blocking read: yield to parent when child blocked on empty pipe ---
    old_pipe_wait = """            while (ps->len == 0 && ps->wr_open > 0) {
                __asm__ volatile("sti; hlt" ::: "memory");
            }"""
    new_pipe_wait = """            while (ps->len == 0 && ps->wr_open > 0) {
                if (g_guest_fork_active && g_coop_side == 1) {
                    return bfree_coop_yield_to_parent();
                }
                if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked) {
                    return bfree_coop_yield_to_child();
                }
                {
                    int er = bfree_guest_sig_take_eintr();
                    if (er < 0) {
                        return er;
                    }
                }
                __asm__ volatile("sti; hlt" ::: "memory");
            }"""
    if old_pipe_wait in text:
        text = text.replace(old_pipe_wait, new_pipe_wait, 1)
        print("OK pipe read yield")
    else:
        print("WARN pipe wait not found (may already be patched)")

    # Pipe write yield to child when parent wrote and child blocked
    if "coop yield after pipe write" not in text:
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
        /* H02: coop yield after pipe write */
        if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked && n > 0) {
            return bfree_coop_yield_to_child();
        }
        if (g_guest_fork_active && g_coop_side == 1 && n > 0 &&
            g_guest_fork_was_as_copy && g_coop_parent_started) {
            return bfree_coop_yield_to_parent();
        }
        return (long)n;
    }
    if (bfree_guest_is_eventfd(fd)) {"""
        if needle_wr in text:
            text = text.replace(needle_wr, repl_wr, 1)
            print("OK pipe write yield")
        else:
            print("WARN pipe write site not found")

PATH.write_text(text, encoding="utf-8", newline="\n")
print("wrote", PATH, "delta", len(text) - len(orig))
