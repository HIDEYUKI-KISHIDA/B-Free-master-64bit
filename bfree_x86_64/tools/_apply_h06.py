#!/usr/bin/env python3
"""H06: job control — setpgid/tty_pgrp sync, TIOC*PGRP, kill stop/cont, soft TTIN/TTOU."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "kernel" / "sysmain" / "syscall.c"
text = PATH.read_text(encoding="utf-8", errors="replace")
orig = text

MARKER = "BFREE_H06_JOBCTL_WIRED"

if MARKER in text:
    print("skip H06 already")
else:
    # --- helpers after soft_job_sig ---
    HELPERS = r'''
/* === H06 job-control / fg path === */
#ifndef BFREE_H06_JOBCTL_WIRED
#define BFREE_H06_JOBCTL_WIRED 1
#define BFREE_LINUX_TIOCGPGRP 0x540F
#define BFREE_LINUX_TIOCSPGRP 0x5410
#define BFREE_SIGTSTP 20
#define BFREE_SIGCONT 18
#define BFREE_SIGSTOP 19
#define BFREE_SIGTTIN 21
#define BFREE_SIGTTOU 22

static long sys_linux_setpgid(long pid, long pgid)
{
    int p = (int)pid;
    int g = (int)pgid;
    long rc;

    if (p < 0 || g < 0) {
        return -22;
    }
    /* pid==0 → calling process (shell or coop child focus). */
    if (p == 0) {
        if (g_guest_fork_active && g_coop_side == 1) {
            p = g_guest_fork_pid;
        } else {
            p = 1; /* init/shell */
        }
    }
    if (g == 0) {
        g = p;
    }
    if (p == 1 || (!g_guest_fork_active && p <= 1)) {
        g_guest_pgid = g;
        /* Shell moving its own pgid does not steal tty until tcsetpgrp. */
        return 0;
    }
    rc = bfree_process_setpgid(p, g);
    if (rc == 0 && g_guest_fork_active && p == g_guest_fork_pid) {
        g_guest_pgid = g;
    }
    return rc;
}

static long sys_linux_getpgid(long pid)
{
    int p = (int)pid;
    int pg;

    if (p < 0) {
        return -22;
    }
    if (p == 0) {
        return (long)bfree_guest_tty_self_pgid();
    }
    if (p == 1) {
        return (long)g_guest_pgid;
    }
    pg = bfree_process_getpgid(p);
    if (pg > 0) {
        return (long)pg;
    }
    return -3; /* ESRCH */
}

static long sys_linux_setsid(void)
{
    /* Single-session guest: become session leader of a new pgid. */
    int sid = g_guest_fork_active ? g_guest_fork_pid : 1;
    g_guest_sid = sid;
    g_guest_pgid = sid;
    /* New session is not yet foreground until tcsetpgrp. */
    return (long)g_guest_sid;
}

/* H06: stop coop child; parent resumes with WIFSTOPPED via wait. */
static long bfree_guest_stop_from_fork(int sig)
{
    int as_copy = g_guest_fork_was_as_copy;
    int pid = g_guest_fork_pid;
    uint64_t parent_fs = g_guest_fork_saved_fsbase;
    int stsig = sig & 0x7f;

    if (stsig == 0) {
        stsig = BFREE_SIGTSTP;
    }
    (void)bfree_process_stop_pid(pid, stsig);
    bfree_guest_sig_raise(17); /* SIGCHLD */
    bfree_coop_fd_switch_to(0);
    if (as_copy) {
        bfree_coop_as_switch_to(0);
        if (g_guest_parent_parked_heap_valid) {
            bfree_guest_load_heap_cwd(g_guest_parent_parked_cwd,
                                      g_guest_parent_parked_heap_next,
                                      g_guest_parent_parked_brk);
        }
    } else {
        bfree_guest_vfork_stack_restore();
        bfree_guest_load_heap_cwd(g_guest_fork_saved_cwd,
                                  g_guest_fork_saved_heap_next,
                                  g_guest_fork_saved_brk);
    }
    g_coop_side = 0;
    g_coop_child_blocked = 1;
    g_coop_parent_started = 1;
    g_guest_fork_active = 1;
    g_guest_fork_pid = pid;
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = parent_fs;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, parent_fs);
    g_bfree_sysret_exec_cr3 = 0;
    bfree_coop_publish_parent_resume();
    if (g_guest_wait_status_ptr != 0 &&
        bfree_user_ptr_mapped(g_guest_wait_status_ptr)) {
        *(int *)(uintptr_t)g_guest_wait_status_ptr = (stsig << 8) | 0x7f;
        g_guest_wait_status_ptr = 0;
    }
    if (g_bfree_fork_parent_ret == 0) {
        g_bfree_fork_parent_ret = (uint64_t)(long)pid;
    }
    return BFREE_SYSRET_FORK_PARENT;
}

#endif /* BFREE_H06_JOBCTL_WIRED */

'''
    needle = "static long bfree_guest_tty_soft_job_sig(int sig)\n{\n    bfree_guest_sig_raise(sig);\n    return bfree_guest_sig_take_eintr();\n}\n"
    if needle not in text:
        raise SystemExit("FAIL soft_job_sig")
    text = text.replace(needle, needle + HELPERS, 1)
    print("OK H06 helpers")

    # --- kill: stop/cont/signal child ---
    old_kill = """static long sys_linux_kill(long pid, long sig)
{
    (void)sig;
    if (pid == 1 || pid == 2 || pid == -1 ||
        (g_guest_fork_active && pid == (long)g_guest_fork_pid) ||
        (g_guest_fork_status_ready && pid == (long)g_guest_fork_pid)) {
        /* Signals are ignored in this single-task guest; succeed for known pids. */
        return 0;
    }
    if (pid == 0) {
        return 0;
    }
    return -3; /* ESRCH */
}
"""
    new_kill = """static long sys_linux_kill(long pid, long sig)
{
    int s = (int)sig;
    int target = (int)pid;

    if (s < 0) {
        return -22;
    }
    if (target < -1) {
        /* process-group kill: map to focused child if pgid matches */
        int pg = (int)(-target);
        if (g_guest_fork_active && bfree_process_getpgid(g_guest_fork_pid) == pg) {
            target = g_guest_fork_pid;
        } else if (pg == g_guest_pgid || pg == g_guest_tty_pgrp) {
            target = 0; /* broadcast to self session — soft ok */
        } else if (!bfree_process_pgid_has_member(pg)) {
            return -3;
        } else {
            target = 0;
        }
    }
    if (s == 0) {
        /* existence check */
        if (target == 0 || target == 1 || target == -1) {
            return 0;
        }
        if (g_guest_fork_active && target == g_guest_fork_pid) {
            return 0;
        }
        if (bfree_process_getpgid(target) > 0) {
            return 0;
        }
        return -3;
    }
    /* Cont stopped child. */
    if (s == BFREE_SIGCONT || s == 18) {
        if (target == g_guest_fork_pid || target == 0 || target == -1) {
            (void)bfree_process_cont_pid(g_guest_fork_pid > 0 ? g_guest_fork_pid : target);
        } else {
            (void)bfree_process_cont_pid(target);
        }
        bfree_guest_sig_raise(s);
        return 0;
    }
    /* Stop: if coop child is running, park it for wait(WUNTRACED). */
    if ((s == BFREE_SIGTSTP || s == BFREE_SIGSTOP || s == 19 || s == 20) &&
        g_guest_fork_active && g_coop_side == 1 &&
        (target == 0 || target == -1 || target == g_guest_fork_pid)) {
        return bfree_guest_stop_from_fork(s);
    }
    if (target == 1 || target == 2 || target == -1 || target == 0 ||
        (g_guest_fork_active && target == g_guest_fork_pid) ||
        (g_guest_fork_status_ready && target == g_guest_fork_pid)) {
        bfree_guest_sig_raise(s);
        return 0;
    }
    return -3; /* ESRCH */
}
"""
    if old_kill in text:
        text = text.replace(old_kill, new_kill, 1)
        print("OK kill stop/cont")
    else:
        print("WARN kill not patched")

    # --- dispatch setpgid/getpgid/setsid ---
    old_cases = """    case 109: /* setpgid — job-control stub (single session) */
        return 0;
    case 111: /* getpgid */
        return (arg1 == 0) ? sys_linux_getpid() : arg1;
    case 112: /* setsid */
        return sys_linux_getpid();
"""
    new_cases = """    case 109: /* setpgid — H06 sync process + guest pgid */
        return sys_linux_setpgid(arg1, arg2);
    case 111: /* getpgid */
        return sys_linux_getpgid(arg1);
    case 112: /* setsid */
        return sys_linux_setsid();
"""
    if old_cases in text:
        text = text.replace(old_cases, new_cases, 1)
        print("OK setpgid/getpgid/setsid cases")
    else:
        print("WARN pgid cases not patched")

    # --- ioctl TIOCGPGRP / TIOCSPGRP ---
    old_winsz = """        if (request == BFREE_LINUX_TIOCGWINSZ) {
            if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                return -14;
            }
"""
    new_winsz = """        if (request == BFREE_LINUX_TIOCGPGRP) {
            if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                return -14;
            }
            *(int *)(uintptr_t)arg = g_guest_tty_pgrp;
            return 0;
        }
        if (request == BFREE_LINUX_TIOCSPGRP) {
            int pg;
            if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                return -14;
            }
            pg = *(int *)(uintptr_t)arg;
            if (pg <= 0) {
                return -22;
            }
            /* fg: ash/busybox tcsetpgrp after setpgid — sync tty foreground. */
            g_guest_tty_pgrp = pg;
            return 0;
        }
        if (request == BFREE_LINUX_TIOCGWINSZ) {
            if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                return -14;
            }
"""
    if old_winsz in text:
        text = text.replace(old_winsz, new_winsz, 1)
        print("OK TIOC*PGRP")
    else:
        print("WARN TIOC*PGRP not patched")

    # --- soft TTIN on background stdin canonical read ---
    # Find a good hook: while (!bfree_stdin_byte_ready) loop or start of stdin read
    if "H06: bg tty soft TTIN" not in text:
        # Hook near ISIG VINTR check / stdin wait
        needle = "        while (!bfree_stdin_byte_ready()) {\n            __asm__ volatile(\"sti; hlt\" ::: \"memory\");\n        }"
        repl = """        /* H06: bg tty soft TTIN */
        if (bfree_guest_tty_is_background()) {
            long er = bfree_guest_tty_soft_job_sig(BFREE_SIGTTIN);
            if (er < 0) {
                return er;
            }
        }
        while (!bfree_stdin_byte_ready()) {
            if (bfree_guest_tty_is_background()) {
                long er = bfree_guest_tty_soft_job_sig(BFREE_SIGTTIN);
                if (er < 0) {
                    return er;
                }
            }
            __asm__ volatile("sti; hlt" ::: "memory");
        }"""
        if needle in text:
            text = text.replace(needle, repl, 1)
            print("OK TTIN on stdin")
        else:
            print("WARN stdin TTIN hook not found")

PATH.write_text(text, encoding="utf-8", newline="\n")
print("wrote delta", len(text) - len(orig))
