# M10 — Sysmain syscalls in kernel.elf

M10 wires the full `kernel/sysmain` syscall table into freestanding `kernel.elf`, replacing the M9 `syscall_min.c` shim (`write` + `exit` only).

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| Freestanding libc stubs | `kernel/freestanding/libc_stubs.c` | (build) |
| Kernel guest bring-up | `kernel/freestanding/guest_kernel.c` | `P10_KERNEL_GUEST` |
| `/dev/console` → debugcon | `devnode.c` | (QEMU) |
| Dispatch wiring | `syscall.c`, `syscall_dispatch.c` | `P10_DISPATCH_WIRING` |
| kernel.elf link | `Makefile` `KERNEL_SYSMAIN_OBJS` | `P9_USER_BOOT_QEMU` |

## Newly dispatched syscalls (examples)

`dup`, `dup2`, `ioctl`, `getpid`, `getppid`, `getpgid`, `unlink`, `openat`, `mkdirat`, `unlinkat`, `getdents64`, plus signal/cap/prlimit stubs.

## Home PC quick start

```bash
git fetch origin
git checkout cursor/m10-syscall-kernel-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_user_boot_smoke.sh   # still expects USER_BOOT_OK
```

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh      # P10_* + RESULT: ALL PASS
./tools/qemu_user_boot_smoke.sh   # USER_BOOT_OK via full write path
```

## Residual (M11+)

- musl static ELF on booted guest (`P11_MUSL_GUEST`)
- `stat` / `poll` / `fstatat`
- BusyBox ash in QEMU initramfs
