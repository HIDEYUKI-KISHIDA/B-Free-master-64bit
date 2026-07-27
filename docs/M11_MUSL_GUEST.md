# M11 — musl static ELF on booted guest

M11 runs a musl-style static `ET_EXEC` from embedded initramfs under ring 3, using the M10 full syscall path (`write` → `/dev/console` → debugcon).

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| ELF load from initramfs blob | `elf_user_load.c` | (kernel) |
| musl in initramfs | `guest/initramfs/musl_static.elf` | `P11_MUSL_GUEST` |
| Boot preference flag | `kernel_boot.c` `BFREE_PREFER_MUSL_BOOT` | `P11_MUSL_GUEST_QEMU` |
| musl hello (raw syscall) | `tools/musl_static/hello.S` | prints `MUSL_STATIC` |

## Home PC quick start

```bash
git fetch origin
git checkout cursor/m11-musl-guest-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_musl_guest_smoke.sh   # needs QEMU; expects MUSL_STATIC
```

Default `kernel.elf` still boots `user_payload.bin` first (P9 `USER_BOOT_OK`). The musl QEMU smoke rebuilds with `KERNEL_BOOT_CFLAGS=-DBFREE_PREFER_MUSL_BOOT=1`.

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh           # P11_MUSL_GUEST + RESULT: ALL PASS
./tools/qemu_user_boot_smoke.sh        # P9 USER_BOOT_OK (default kernel)
./tools/qemu_musl_guest_smoke.sh       # P11 MUSL_STATIC (musl-preferred kernel)
```

## Residual (M12+)

- Guest LTP / POSIX regression on booted QEMU (M13)
- `stat` / `poll` / `fstatat` for fuller libc coverage
- `execve` from guest VFS without kernel hand-boot
