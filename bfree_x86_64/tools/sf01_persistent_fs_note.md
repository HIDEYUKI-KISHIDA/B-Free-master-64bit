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

## Concrete next steps (Phase 7)

1. Add a simple ramdisk or host-backed block image (e.g. `persist.img` as a GRUB module / multiboot module).
2. Implement a minimal block layer (`read_block` / `write_block`) over that image.
3. Port or write a tiny ext2 (or FAT) read/write: superblock, inode, directory, single-block files first.
4. Mount at `/persist` replacing the vfile prefix; keep vfile fallback until mount succeeds.
5. Gate with a smoke: `echo x >/persist/a; /busybox.elf sh -c 'cat /persist/a'` then reboot and verify durability once block image is wired.

## Related holes

- H08 (`writable-beyond-tmp`) already covers `/var`+`/home`.
- H28 meta notes SF-01 partial via `/persist` vfile; full block FS remains arch/deferred.
