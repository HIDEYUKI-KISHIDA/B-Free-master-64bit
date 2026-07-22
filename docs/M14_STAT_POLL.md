# M14 — stat / poll / fstatat syscalls + guest probes

M14 implements `stat` (4), `fstat` (5), `poll` (7), and `newfstatat` (262) on the guest syscall path, plus ring-3 QEMU probes that exercise them on a booted guest.

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| VFS stat fill | `fs_ofd.c` `bfree_vnode_fill_stat` | (kernel) |
| poll on pipes/files | `process.c` `bfree_poll` | (kernel) |
| Syscall dispatch | `syscall.c`, `syscall_dispatch.c` | `P14_STAT_POLL` |
| stat guest probe | `guest/initramfs/stat_guest.elf` | `STAT_GUEST_OK` |
| poll guest probe | `guest/initramfs/poll_guest.elf` | `POLL_GUEST_OK` |
| Boot flags | `BFREE_PREFER_STAT_GUEST_BOOT`, `BFREE_PREFER_POLL_GUEST_BOOT` | QEMU smokes |

### Syscalls added

| NR | Name | Behaviour |
|----|------|-----------|
| 4 | `stat` | Fill `struct stat` from vnode lookup |
| 5 | `fstat` | Stat via open fd |
| 7 | `poll` | Non-blocking readiness for pipes and regular files |
| 262 | `newfstatat` | Stat with `dirfd` + relative path (`AT_FDCWD` supported) |

`poll` does not block on positive timeout yet (returns immediately); sufficient for pipe readiness probes.

## Home PC quick start

```bash
git fetch origin
git checkout cursor/m14-stat-poll-fstatat-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_stat_guest_smoke.sh    # STAT_GUEST_OK (needs QEMU)
./tools/qemu_poll_guest_smoke.sh    # POLL_GUEST_OK (needs QEMU)
```

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh        # P14_* + RESULT: ALL PASS
./tools/qemu_stat_guest_smoke.sh
./tools/qemu_poll_guest_smoke.sh
```

## Residual (M15+)

- Blocking `poll` / `ppoll` with timeout
- Guest `ash_regress` on QEMU (needs `execve` from VFS)
- Migrate remaining `ltp_regress` / `posix_regress` host cases to guest probes
- Full upstream LTP checkout (out of scope)
