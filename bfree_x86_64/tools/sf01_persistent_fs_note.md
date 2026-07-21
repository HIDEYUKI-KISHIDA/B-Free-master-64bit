# SF-01 — Persistent block filesystem (Phase 7 gap)

## Status (2026-07-22)

Honest progress only — **not** a real on-disk ext2/block FS.

| Deliverable | State |
|---|---|
| Full ext2 / block device FS | **Deferred** (Phase 7 full / beyond P4 agreed scope) |
| In-boot vfile NS across `execve` | **Already true** for `g_guest_vfiles[]` (exec reset clears heap/pipes/fds, not vnodes) |
| Minimal `/persist` namespace | **Added** — same vfile backing as `/tmp`/`/var`/`/home`, prefix `persist/` |
| P4 agreed gate | **Green** — RAM vfile documented; see `docs/POSIX_PHASE7_AGREED_SCOPE.md` |

## What `/persist` is

- Writable path prefix `/persist` and `/persist/<name>` mapped into the guest vfile table.
- Survives applet `execve` / BusyBox re-exec within the same boot (vnode table is global).
- Does **not** survive reboot, power-off, or ISO remount — RAM-only.

## Implemented (2026-07-22, F1)

1. **ATA PIO block layer** — `bfree_ata_rw_sector` (LBA28, primary master, polled
   with nIEN) in `kernel/sysmain/syscall.c`.
2. **BFP1 record store** — LBA0 superblock (`BFP1` + count), each record =
   1 header sector (name[48]+len) + 32 data sectors, mirroring `persist/` vfiles.
3. **Hooks** — lazy `bfree_persist_load_once()` on first `/persist` path
   resolution; `bfree_persist_flush_all()` after write/ftruncate/unlink.
4. **Reboot smoke green** — `tools/_f1_persist_smoke.sh`: boot1 writes
   `/persist/f1`, boot2 (same `persist.img`) reads it back.
   QEMU wiring: `-drive file=persist.img,if=ide,index=0,media=disk,format=raw`.

## Remaining (Phase 7 full)

- tiny FAT/ext2 mount at `/persist` (replace BFP1 records with a real FS).
- AHCI (`sata_ahci_*`) instead of legacy ATA PIO if needed for real HW.

## Related holes

- H08 (`writable-beyond-tmp`) already covers `/var`+`/home`.
- H28 meta notes SF-01 partial via `/persist` vfile; full block FS remains arch/deferred.
