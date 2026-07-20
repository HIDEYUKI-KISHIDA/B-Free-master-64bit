# Recovered syscall.c fragments

Artifacts from agent-transcript mining (`97982f27` and related subagents).
**Do not treat these as a drop-in replacement** — merge manually onto HEAD after the patch-script layers below.
`kernel/sysmain/syscall.c` was **not** modified by this recovery pass.

## Apply order (relative to `tools/_patch_*.py`)

Start from **git HEAD** `syscall.c` (~225 KiB), then:

| Step | Action | What it restores |
|------|--------|------------------|
| 1 | `py -3 tools/_patch_stage1_b.py` | Small wait-status / vfork resume tweaks |
| 2 | `py -3 tools/_patch_stage1_ash.py` | cloexec, parent-first fork return, stdin/wait yield, `/home` |
| 3 | `py -3 tools/_patch_unix_coop.py` | Injects `tools/_unix_coop_block.c` (AF_UNIX + early coop yields **without** CR3/`as_copy` heap park), pipe yield hooks, socket dispatch |
| 4 | `py -3 tools/_patch_pty_stage2.py` | `/dev/ptmx`, pts, TIOCGPTN, early CATCH/uid stubs (**see FD conflict**) |
| 5 | `py -3 tools/_patch_posix_phase_a.py` | Signal pending/mask/alarm, robust_list, flock, etc. |
| 6 | `py -3 tools/_patch_posix_phase_a2.py` | More ABI helpers / tkill / dispatch |
| 7 | `py -3 tools/_patch_sf_todos.py` | SF-01/02 path wiring leftovers |
| 8 | `py -3 tools/_patch_watch2.py` | Watch-phys probes (also touches `elf_loader.c`) |
| — | `tools/_patch_clone_va.py` | **vmm.c only** (not syscall.c) |

### Direct-edit layers (this directory — not covered by `_patch_*.py`)

Apply **after** steps 1–3 at minimum (unix/coop base required). Prefer after posix/pty if needles still match.

| File | Contents | Confidence |
|------|----------|------------|
| `unix_coop_base.md` | Pointer to `_unix_coop_block.c` | high (on-disk) |
| `as_copy.c` | `g_guest_fork_was_as_copy`, dual parked heap/cwd, `bfree_guest_as_copy_switch_heap_to_*`, `bfree_coop_as_switch_to`, yield_* with heap park, fork parent-first AS-copy return, wait yield, `stop_from_fork`, `exit_from_fork` fragment | **high** for core functions |
| `coop_as_switch.c` | Standalone CR3 switch + `save_child_user` (also in `as_copy.c`) | high |
| `inet.c` | `g_inet_socks` / AF_INET types, helpers, bind/listen/connect/accept/sendto/recvfrom, readiness, sockopts, routable/match | **high** (large coherent blocks); bind/connect may need later `bfree_inet_is_guest_routable` wiring over plain loopback checks |
| `exec_transfer.c` | `g_bfree_exec_transfer_rip` decl + set/clear snippets | high for decl/set; glue into execve carefully |
| `futex.c` | H20 waiter-slot `sys_futex` (replaces HEAD stub) | high |
| `thread_clone.c` | `bfree_guest_thread_*` / CLONE_THREAD helpers | medium–high (dispatch gate incomplete) |
| `sigframe.c` | H01 `bfree_rt_sigframe_t` + partial `bfree_guest_sig_try_deliver` | medium (overlaps posix patches; truncated) |
| `ptmx_from_patch.md` | Use `_patch_pty_stage2.py` | high for process; **FD clash** |
| `coop_sessions.md` | Dual-live session API **not recovered** as complete functions | n/a |

**FD conflict:** `_patch_pty_stage2.py` uses `BFREE_PTY_MASTER_BASE 0x3a00`; recovered `inet.c` uses `BFREE_INET_FD_BASE 0x3A00`. Rebase one before combining.

## Symbol coverage (vs `syscall.o` interest set missing from HEAD)

Interest symbols in `syscall.o` but absent from HEAD source: **132**.

Present in recovery `*.c` artifacts: **66** (see `_recovered_syms.txt`).

Still missing after this recovery: **66** (see `_still_missing.txt`). Notable gaps:

### Critical / expected from latest tree but **not** recovered as complete bodies
- `bfree_coop_session_*` / `bfree_coop_sessions_init` (H02 dual-live) — only call-site mentions in transcripts
- `bfree_coop_publish_child_resume`, `bfree_coop_save_parent_user`, `bfree_coop_wait_yield_if_live_child`, `bfree_guest_exit_coop_rebind`
- Full CATCH/`bfree_guest_sig_*` suite beyond partial sigframe (much may come from posix patches once applied)
- `g_bfree_sig_saved_*` (asm handoff) — may live partly outside syscall.c
- tty job-control helpers (`bfree_guest_tty_*`, `g_guest_tty_pgrp`)
- shm/alias/vfile extras, elf watch phys globals, `bfree_guest_shell_reenter_after_fatal`

### Covered well by recovery artifacts
- `g_guest_fork_was_as_copy`, parked heap globals, `bfree_guest_as_copy_switch_heap_to_{child,parent}`
- `bfree_coop_as_switch_to`, upgraded coop yields
- `g_inet_socks` / `bfree_inet_*`, `g_unix_socks` (also via `_unix_coop_block.c`)
- `g_bfree_exec_transfer_rip`
- H20 futex waiters, thread clone helpers

### Already in HEAD (no recovery needed for symbols)
- timerfd family (`sys_timerfd_*`, `g_timerfd_entries`, …)
- stub `sys_futex` (superseded by `futex.c`)

## Raw mining leftovers
- `_raw_hits/` — deduped StrReplace/Write JSON payloads
- `_frags/` — individual theme dumps before assembly
- `_o_interest_syms.txt` / `_missing_vs_source.txt` — ELF vs HEAD diffs
- Helper scripts: `_mine_transcripts.py`, `_assemble.py`, …

## Confidence summary
- **as_copy + inet + coop CR3 switch:** high (complete functions from successful StrReplace payloads)
- **futex H20 + thread_clone + exec_transfer decl:** high–medium
- **sigframe:** medium (partial)
- **coop sessions / dual-live:** low — **not recovered**; must reconstruct from design notes or binary
- **ptmx:** use patch script (high for that layer), resolve FD base vs inet
