# M12 — BusyBox ash on booted guest

M12 runs static BusyBox `ash` from embedded initramfs under ring 3, using the M10 syscall path. A small ring-3 trampoline at `0x100000` sets Linux ABI `argc`/`argv`/`envp` before jumping to the BusyBox entry point.

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| BusyBox in initramfs | `guest/initramfs/bin/busybox` | `P12_BUSYBOX_GUEST` |
| ELF load from initramfs | `elf_user_load.c` | (kernel) |
| argv trampoline | `ash_guest_tramp.S`, `ash_guest_boot.c` | (kernel) |
| Boot preference flag | `kernel_boot.c` `BFREE_PREFER_BUSYBOX_BOOT` | `P12_BUSYBOX_GUEST_QEMU` |
| ash one-liner | `busybox ash -c 'echo ASH_GUEST_OK'` | prints `ASH_GUEST_OK` |

## Home PC quick start

```bash
git fetch origin
git checkout cursor/m12-busybox-guest-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_busybox_guest_smoke.sh   # needs QEMU; expects ASH_GUEST_OK
```

Default `kernel.elf` still boots `user_payload.bin` first (P9 `USER_BOOT_OK`). The BusyBox QEMU smoke rebuilds with `KERNEL_BOOT_CFLAGS=-DBFREE_PREFER_BUSYBOX_BOOT=1`.

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh              # P12_BUSYBOX_GUEST + RESULT: ALL PASS
./tools/qemu_user_boot_smoke.sh           # P9 USER_BOOT_OK (default kernel)
./tools/qemu_musl_guest_smoke.sh        # P11 MUSL_STATIC (musl-preferred kernel)
./tools/qemu_busybox_guest_smoke.sh     # P12 ASH_GUEST_OK (busybox-preferred kernel)
```

## Residual (M14+)

- Additional guest LTP/POSIX probe migration (M13 partial)
- `stat` / `poll` / `fstatat` for fuller libc and ash coverage
- `execve` from guest VFS without kernel hand-boot
