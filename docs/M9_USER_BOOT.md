# M9 — Ring-3 user boot + in-kernel syscall path

M9 is the **first milestone that closes the biggest POSIX hole**: userspace must run under `kernel.elf` with real `syscall`/`sysret`, not the host Linux shim.

**Target arch:** x86_64 (`bfree_x86_64/`). Not aarch64.

## Home PC quick start

```bash
git clone https://github.com/HIDEYUKI-KISHIDA/B-Free-master-64bit.git
cd B-Free-master-64bit
git fetch origin
git checkout cursor/m9-user-boot-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
```

Open the repo in **Cursor on your PC**, then paste into chat:

> Continue M9 on branch `cursor/m9-user-boot-695c`. Goal: `USER_BOOT_OK` from QEMU. See `docs/M9_USER_BOOT.md`.

## M9 checklist

| # | Task | Marker | Files |
|---|------|--------|-------|
| 1 | GDT build + `lgdt` in ring 0 | `P9_GDT` | `gdt.c` (host test done) |
| 2 | Point LSTAR at `bfree_syscall_insn_entry` | — | `trap_setup.c`, `kernel_boot.c` |
| 3 | Per-CPU kernel stack for `syscall` entry | — | `syscall_insn_entry.S` |
| 4 | Map user pages + load ELF from initramfs | — | `user_boot.c`, `elf_load.c` port |
| 5 | `iretq` to ring 3 | `P9_USER_BOOT` | `user_boot.c` (host skip done) |
| 6 | musl payload using raw `syscall` | `P9_MUSL_KERNEL` | initramfs + QEMU |
| 7 | QEMU prints `USER_BOOT_OK` | `P9_USER_BOOT_QEMU` | `qemu_user_boot_smoke.sh` |

## Acceptance

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh          # must stay ALL PASS
./build/host-tests/test_p9_gdt
./build/host-tests/test_p9_user_boot
./tools/qemu_user_boot_smoke.sh       # goal: PASS with USER_BOOT_OK
```

## Suggested order (for Cursor on home PC)

1. Wire `bfree_gdt_install()` into `bfree_kernel_boot()` before trap install.
2. Set `syscall_lstar` to `bfree_syscall_insn_entry` in freestanding build.
3. Add minimal user ELF (asm `syscall` write + exit) to initramfs.
4. Implement `bfree_user_boot_exec()` with valid user page tables.
5. Print `USER_BOOT_OK` from kernel after user payload returns or writes.

## After M9 (POSIX hole filling continues)

- **M10:** `stat*`, `getdents64`, `poll`, `munmap`/`mprotect`, registry gaps
- **M11:** `ioctl`/`termios`, `listen`/`accept`, signal depth
- **M12:** musl dynamic linker + pthread futex
- **M13:** BusyBox on real guest + upstream LTP subset

## PR stack

Base: `cursor/m8-real-boot-695c` → this branch → (your M9 PR on GitHub)
