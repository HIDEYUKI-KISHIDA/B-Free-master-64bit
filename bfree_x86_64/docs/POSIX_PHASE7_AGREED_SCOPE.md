# Phase 7 — Agreed scope for P4 gate (2026-07-22)

This document defines the **agreed** Phase 7 subset that closes
`docs/POSIX_COMPAT_TODOLIST.md` **P4**. Full M5 (block FS, real NIC, LTP claim,
zero unintentional ENOSYS) remains **deferred**.

## Agreed green (this PR)

| ID | Agreed deliverable | Evidence |
|----|--------------------|----------|
| **T-P4-1** | `/persist` = **RAM vfile** NS (survives in-boot execve; not reboot) | `tools/sf01_persistent_fs_note.md`; phase3 `p8_persist` |
| **T-P4-2** | **Pipe-backed** `AF_INET` stub: `127/8` + `10.0.2.0/24` remap (no NIC) | phase3 `p8_inet` / `p8_slirp` |
| **T-P4-3** | Unhandled Linux nr → **ENOSYS (−38)** via dispatch default; intentional residual | `BFREE_LINUX_SYSCALL_UNHANDLED`; inventory via `tools/_p4_agreed_gate.sh` |
| **T-P4-4** | LTP gate = **SKIP if not vendored** + `tools/ltp_selftests/` | `tools/ltp_subset_gate.sh` → `.cache/ltp_gate_status.txt` |

## Explicitly deferred (not P4 blockers)

1. `persist.img` / block layer / tiny ext2|FAT over Multiboot module
2. Real host networking (virtio-net / tap)
3. Driving every unimplemented syscall off ENOSYS (beyond intentional residual)
4. Vendored LTP tree + curated QEMU subset runner

## How to re-verify

```bash
bash tools/_p4_agreed_gate.sh
# optional: BFREE_PHASE3_REUSE_BUILD=1 bash tools/phase3_guest_auto.sh
```

## Roadmap mapping

`docs/POSIX_FULL_COMPAT_ROADMAP.ja.md` Phase 7 checkboxes stay **partially open**
for deferred items; STATUS marks the **agreed subset** green.
