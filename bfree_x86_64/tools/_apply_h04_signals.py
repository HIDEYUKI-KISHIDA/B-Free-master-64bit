#!/usr/bin/env python3
"""Minimal H04 signal pending/mask + replace soft stubs from unix coop."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "kernel" / "sysmain" / "syscall.c"
text = PATH.read_text(encoding="utf-8", errors="replace")

if "g_guest_sig_pending" in text:
    print("signal pending already present")
else:
    block = """
/* H04: real POSIX signal pending/mask (minimal) */
#define BFREE_NSIG 64
#define BFREE_SIG_DFL 0
#define BFREE_SIG_IGN 1
#define BFREE_SIG_CATCH 2
static uint8_t g_guest_sig_disp[BFREE_NSIG];
static uint64_t g_guest_sig_pending;
static uint64_t g_guest_sig_mask;

static void bfree_guest_sig_raise(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return;
    }
    if (g_guest_sig_disp[sig] == BFREE_SIG_IGN) {
        return;
    }
    g_guest_sig_pending |= (1ULL << (unsigned)sig);
}

static int bfree_guest_sig_is_blocked(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return 0;
    }
    return (g_guest_sig_mask & (1ULL << (unsigned)sig)) != 0;
}

static int bfree_guest_sig_take_eintr(void)
{
    uint64_t pend = g_guest_sig_pending & ~g_guest_sig_mask;
    if (pend == 0) {
        return 0;
    }
    /* Leave pending for wait/handlers; interrupt blocking ops. */
    return -4; /* EINTR */
}

static void bfree_guest_alarm_poll(void)
{
    /* filled by H13; no-op until setitimer wired */
}

"""
    # Remove soft stubs if present
    soft = """/* Soft stubs until H04/H13 signal pending is restored */
static void bfree_guest_alarm_poll(void) {}
static int bfree_guest_sig_take_eintr(void) { return 0; }

"""
    if soft in text:
        text = text.replace(soft, "", 1)
        print("OK removed soft stubs")
    needle = "static long bfree_guest_fork_enter(void)\n"
    # Prefer insert near other guest globals after stubs section
    mark = "/* --- post-wipe stubs for advanced syscall_entry.S (filled by hole redo) --- */"
    if mark in text:
        # after sig_saved_r15 block
        idx = text.find("uint64_t g_bfree_sig_saved_r15;")
        if idx > 0:
            end = text.find("\n\n", idx)
            text = text[: end + 2] + block + text[end + 2 :]
            print("OK signal block after stubs")
        else:
            text = text.replace(needle, block + needle, 1)
            print("OK signal block before fork_enter")
    else:
        text = text.replace(needle, block + needle, 1)
        print("OK signal block before fork_enter")

# Wire SIGCHLD on child exit if marker exists
old = "    g_guest_fork_status_ready = 1;"
# Don't blindly raise; look for exit_from_fork style. Keep minimal: on status ready path already sets ready.

PATH.write_text(text, encoding="utf-8", newline="\n")
print("wrote", PATH.stat().st_size)
