# -*- coding: utf-8 -*-
import json
from pathlib import Path

raw = Path("tools/_recovered_syscall/_raw_hits")
out = Path("tools/_recovered_syscall")


def load_hit(name: str) -> str:
    return json.loads((raw / name).read_text(encoding="utf-8"))["new_string"]


def w(name: str, text: str) -> None:
    body = text.strip() + "\n"
    (out / name).write_text(body, encoding="utf-8")
    print("wrote", name, len(body))


as_copy_parts = [
    """/* Recovered AS-copy / parked-heap / CR3 switch / coop yield hooks.
 * Sources: 97982f27, 0ce9e498, cf433436 agent transcripts (StrReplace payloads).
 * Apply AFTER tools/_patch_unix_coop.py (needs coop FD snap) and stage1 fork parent-first.
 */

/* --- globals (merge carefully; some fork_* already exist in HEAD) --- */

/* 1 if this coop child was created via SYS_fork AS-copy (parent kept running).
 * Distinct from has_private_as: vfork+exec also gains a private AS, but the
 * parent was frozen and still needs the shared-AS stack snapshot restored. */
static int g_guest_fork_was_as_copy;

/* AS-copy: kernel brk/cwd are global. Dual-park across coop switches so each
 * side resumes its own markers. On child exit restore parent park (not
 * fork-enter) so post-fork parent brk is not rewound. First child resume uses
 * fork-enter (birth heap) until the child gets its own park slot. */
static int g_guest_parent_parked_heap_valid;
static uint64_t g_guest_parent_parked_heap_next;
static uint64_t g_guest_parent_parked_brk;
static char g_guest_parent_parked_cwd[256];
static int g_guest_child_parked_heap_valid;
static uint64_t g_guest_child_parked_heap_next;
static uint64_t g_guest_child_parked_brk;
static char g_guest_child_parked_cwd[256];
""",
    load_hit("hit_026_97982f27.json").strip(),
    """/* H02: AS-copy coop must flip CR3 with the logical parent/child side. */
static void bfree_coop_as_switch_to(int side)
{
    page_table_t *pt;

    if (!knl_current_task) {
        return;
    }
    if (!bfree_process_child_has_private_as()) {
        return; /* vfork / shared AS — nothing to flip */
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
}
""",
    load_hit("hit_017_97982f27.json").strip(),
    load_hit("hit_066_cf433436.json").strip(),
    load_hit("hit_032_97982f27.json").strip(),
]

fork_enter_blob = load_hit("hit_021_97982f27.json")
idx = fork_enter_blob.find("static long bfree_guest_stop_from_fork")
if idx >= 0:
    rest = fork_enter_blob[idx:]
    end = rest.find("/* Blocking wait")
    if end > 0:
        as_copy_parts.append(rest[:end].strip())

as_copy_parts.append(load_hit("hit_040_0ce9e498.json").strip())
w("as_copy.c", "\n\n".join(as_copy_parts))

inet_parts = [
    """/* Recovered AF_INET (+ dual-path unix) socket layer from f40d0181 / fa9a295b / 97982f27.
 * Extends tools/_unix_coop_block.c (AF_UNIX only). Direct edits — NOT in _patch_*.py.
 * NOTE: BFREE_INET_FD_BASE 0x3A00 collides with _patch_pty_stage2.py PTY_MASTER_BASE —
 * reconcile FD bases when merging.
 */
""",
    load_hit("hit_045_f40d0181.json").strip(),
    load_hit("hit_027_f40d0181.json").strip(),
    load_hit("hit_054_97982f27.json").strip(),
]
rel = load_hit("hit_084_f40d0181.json")
rel_i = rel.find("static void bfree_inet_sock_release")
if rel_i >= 0:
    inet_parts.append(rel[rel_i:].strip())
inet_parts.append(load_hit("hit_003_f40d0181.json").strip())
inet_parts.append(load_hit("hit_008_fa9a295b.json").strip())
unix = load_hit("hit_041_f3663731.json")
inet_parts.append(
    "/* unix_from_fd / related (may already exist after _patch_unix_coop.py) */\n"
    + unix.strip()
)
w("inet.c", "\n\n".join(inet_parts))

w(
    "futex.c",
    "/* Recovered H20 futex (waiter slots + timed wait). Replaces HEAD stub sys_futex.\n"
    " * Source: 97982f27 StrReplace.\n"
    " */\n\n"
    + load_hit("hit_012_97982f27.json"),
)

w(
    "thread_clone.c",
    "/* Recovered guest CLONE_THREAD helpers (inlined in syscall.c).\n"
    " * Source: 97982f27. Related: bfree_guest_thread_*, g_guest_thread_*.\n"
    " * Clone dispatch CLONE_THREAD gate may be a separate small patch.\n"
    " */\n\n"
    + load_hit("hit_018_97982f27.json"),
)

sig = [
    """/* Recovered H01 rt_sigframe / CATCH delivery pieces.
 * Sources: 97982f27 (frame layout), f3663731 (sig_try_deliver fragment).
 */
""",
    load_hit("hit_034_97982f27.json").strip(),
]
big = load_hit("hit_007_f3663731.json")
i = big.find("static long bfree_guest_sig_try_deliver")
if i >= 0:
    chunk = big[i : i + 4500]
    # cut at a plausible next major comment if present late
    cut = chunk.find("\n/* ", 200)
    if cut > 2000:
        chunk = chunk[:cut]
    sig.append(chunk.strip())
w("sigframe.c", "\n\n".join(sig))

execb = [
    """/* Recovered exec-transfer RIP handoff for BFREE_SYSRET_EXEC_TRANSFER.
 * Decl used by syscall_entry.S (.extern g_bfree_exec_transfer_rip).
 * Sources: 97982f27.
 */

/* Exec-only user RIP for BFREE_SYSRET_EXEC_TRANSFER (coop must not clobber). */
uint64_t g_bfree_exec_transfer_rip;
""",
    "/* Set path (inside execve / ELF load success): */",
    load_hit("hit_083_97982f27.json").strip(),
    "/* Clear path: */",
    load_hit("hit_111_97982f27.json").strip(),
]
w("exec_transfer.c", "\n\n".join(execb))

w(
    "coop_as_switch.c",
    "/* bfree_coop_as_switch_to + save_child_user (cf433436). Also inlined in as_copy.c. */\n\n"
    + load_hit("hit_050_cf433436.json"),
)

w(
    "ptmx_from_patch.md",
    """# ptmx recovery

PTY/ptmx was applied via `tools/_patch_pty_stage2.py`, not a single transcript dump of final C.

Re-apply: `py -3 tools/_patch_pty_stage2.py` on HEAD `syscall.c`.

**FD base conflict:** that script uses `BFREE_PTY_MASTER_BASE 0x3a00`, while recovered `inet.c` uses `BFREE_INET_FD_BASE 0x3A00`. Reconcile before merging both.
""",
)

# Also copy unix base note
w(
    "unix_coop_base.md",
    """# AF_UNIX + early coop base

Canonical early layer already on disk:

- `tools/_unix_coop_block.c` — AF_UNIX sockets + early `bfree_coop_yield_*` (no CR3 flip, no as_copy heap park)
- Applied by `tools/_patch_unix_coop.py`

Recovered `as_copy.c` / `inet.c` supersede pieces of that block (yields gain `bfree_coop_as_switch_to` + heap park; socket() gains AF_INET).
""",
)

print("assemble done")
