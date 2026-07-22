# M13 — Guest LTP/POSIX regression on booted QEMU

M13 adds ring-3 guest regression probes that mirror the smallest host `ltp_regress` / `posix_regress` cases, running under the M10 syscall trap on a booted QEMU guest. Host gates (`phase3_guest_ltp.sh`, `phase3_guest_posix_regress.sh`) remain unchanged; M13 is an additive QEMU path.

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| LTP open probe | `guest/initramfs/ltp_open_guest.elf` | `LTP_OPEN_GUEST_OK` |
| POSIX I/O probe | `guest/initramfs/posix_io_guest.elf` | `POSIX_IO_GUEST_OK` |
| Probe sources | `tools/guest_regress/*.S` | raw syscall ET_EXEC |
| Boot flags | `kernel_boot.c` `BFREE_PREFER_LTP_OPEN_BOOT`, `BFREE_PREFER_POSIX_IO_BOOT` | QEMU smokes |
| Staging check | `test_p13_guest_regress.c` | `P13_GUEST_REGRESS` |

### Guest probes

| Probe | Syscalls exercised | Host analogue |
|-------|-------------------|---------------|
| `ltp_open_guest.elf` | `open`, `write`, `close` on `/dev/console` | `ltp_regress/01_open_invoke.sh` |
| `posix_io_guest.elf` | `pipe`, `write`, `read`, `write` to stdout | `posix_regress/03_io.sh` |

## Home PC quick start

```bash
git fetch origin
git checkout cursor/m13-guest-ltp-posix-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_ltp_open_guest_smoke.sh      # LTP_OPEN_GUEST_OK (needs QEMU)
./tools/qemu_posix_io_guest_smoke.sh      # POSIX_IO_GUEST_OK (needs QEMU)
```

Default `kernel.elf` still boots `user_payload.bin` first (P9 `USER_BOOT_OK`). QEMU smokes rebuild with `KERNEL_BOOT_CFLAGS=-DBFREE_PREFER_LTP_OPEN_BOOT=1` or `-DBFREE_PREFER_POSIX_IO_BOOT=1`.

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh              # P13_GUEST_REGRESS + RESULT: ALL PASS
./tools/qemu_ltp_open_guest_smoke.sh
./tools/qemu_posix_io_guest_smoke.sh
```

Host LTP/POSIX scripts still run in `phase3_guest_auto.sh` before guest QEMU smokes.

## Residual (M14+)

- Migrate additional `ltp_regress` / `posix_regress` cases to guest probes
- `stat` / `poll` / `fstatat` syscalls for fuller libc and ash coverage
- Guest `ash_regress` on QEMU (needs `execve` from VFS)
- Full upstream LTP checkout (out of scope for this track)
