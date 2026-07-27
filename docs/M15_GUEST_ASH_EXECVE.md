# M15 — Guest ash regress on QEMU + ring-3 execve

M15 seeds the synthetic VFS from initramfs, implements ring-3 `execve` for ELF files on that VFS, adds blocking `poll` (busy-wait timeout), and runs a BusyBox ash regress slice on booted QEMU.

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| VFS seed from initramfs | `guest_initramfs_seed.c` | (kernel) |
| Ring-3 execve | `elf_user_exec.c`, `guest_exec_tramp.S` | `execve` syscall |
| Blocking poll | `process.c` `bfree_poll` | `P15_POLL_BLOCKING` |
| Ash regress QEMU boot | `ash_regress_guest_boot.c` | `ASH_*_GUEST_OK` |
| Initramfs scripts | `guest/initramfs/ash_regress/*.sh` | `P15_ASH_REGRESS_STAGED` |

### Ash regress on QEMU (M15 slice)

Builtin-only cases (no fork) run via `busybox ash -c`:

- Subshell isolation → `ASH_SUBSHELL_GUEST_OK`
- Command substitution with `echo` builtin → `ASH_CMDSUBST_GUEST_OK`

Host `phase3_guest_ash.sh` still runs the full five-script suite on Linux.

### Ring-3 execve

`execve` loads an `ET_EXEC` from the seeded VFS (`/bin/busybox`, `/bin/true`, …), builds an argv trampoline at `0x100000`, and `iretq`s into the new program via `bfree_syscall_exec_resume_if_needed`.

## Home PC quick start

```bash
git fetch origin
git checkout cursor/m15-guest-ash-execve-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_ash_regress_guest_smoke.sh   # needs QEMU
```

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh              # P15_* + RESULT: ALL PASS
./tools/qemu_ash_regress_guest_smoke.sh
```

Rebuild ash regress kernel with `KERNEL_BOOT_CFLAGS=-DBFREE_PREFER_ASH_REGRESS_BOOT=1`.

## Residual (M17+)

- Per-process FD tables
- Preemptive scheduling / timer ticks
- Migrate remaining host LTP/POSIX probes to guest