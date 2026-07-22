# B-Free-master-64bit

64-bit open-source OS inheriting BTRON philosophy, with a Linux/POSIX guest compatibility track.

## POSIX compatibility roadmap

See [docs/POSIX_FULL_COMPAT_ROADMAP.ja.md](docs/POSIX_FULL_COMPAT_ROADMAP.ja.md) for milestone tracking (M0–M10).

### Run verification (host tests)

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh
```

Expected output: `RESULT: ALL PASS`

### Block volume (M1 persistent FS)

```bash
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # includes P4_BLOCK_FS
```

### Process model (M2)

```bash
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # includes P4_VFORK_EXEC, P4_WAITID, P4_FORK, P4_PIPE_SIGNAL
```

### Normal CLI (M3)

```bash
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P4_SHELL_CLI + guest ash (ASH_*)
```

Guest rootfs is built from **upstream BusyBox** (no B-Free shell hacks). See [docs/M3_BUSYBOX_PATCH_ROLLBACK.md](docs/M3_BUSYBOX_PATCH_ROLLBACK.md).

### musl / static binary base (M4)

```bash
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P4_RW_DUP2, P4_BRK_MMAP, P4_ELF_LOAD, P4_CLONE_FUTEX, P4_TTY_JOB, P4_MUSL_LOAD, P4_MUSL_EXEC
```

Build musl-style static test binary: `./tools/build_musl_static.sh`

### POSIX surface (M5)

```bash
cd bfree_x86_64 && ./tools/phase3_guest_posix_regress.sh  # POSIX_REGRESS: ALL PASS
```

See [docs/M5_POSIX_GATE.md](docs/M5_POSIX_GATE.md).

### Boot / syscall trap (M6)

```bash
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P6_SYSCALL_INVOKE, P6_TRAP_ENTRY, P6_BOOT_SMOKE
```

See [docs/M6_BOOT_TRAP.md](docs/M6_BOOT_TRAP.md).

### Kernel boot / in-kernel trap (M7)

```bash
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P7_* + LTP_REGRESS
```

See [docs/M7_KERNEL_BOOT.md](docs/M7_KERNEL_BOOT.md).

### Real boot path (M8)

```bash
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P8_* + embedded initramfs kernel
```

See [docs/M8_REAL_BOOT.md](docs/M8_REAL_BOOT.md).

### Ring-3 user boot (M9)

```bash
git checkout cursor/m9-user-boot-695c
cd bfree_x86_64 && bash tools/setup_home_dev.sh
./tools/qemu_user_boot_smoke.sh   # USER_BOOT_OK (needs QEMU)
```

See [docs/M9_USER_BOOT.md](docs/M9_USER_BOOT.md) and [docs/HOME_PC_SETUP.md](docs/HOME_PC_SETUP.md).

### Sysmain in kernel.elf (M10)

```bash
git checkout cursor/m10-syscall-kernel-695c
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P10_* + RESULT: ALL PASS
```

See [docs/M10_SYSCALL_KERNEL.md](docs/M10_SYSCALL_KERNEL.md).

### musl on booted guest (M11)

```bash
git checkout cursor/m11-musl-guest-695c
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P11_MUSL_GUEST
./tools/qemu_musl_guest_smoke.sh                 # MUSL_STATIC (needs QEMU)
```

See [docs/M11_MUSL_GUEST.md](docs/M11_MUSL_GUEST.md).

### BusyBox ash on booted guest (M12)

```bash
git checkout cursor/m12-busybox-guest-695c
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P12_BUSYBOX_GUEST
./tools/qemu_busybox_guest_smoke.sh              # ASH_GUEST_OK (needs QEMU)
```

See [docs/M12_BUSYBOX_GUEST.md](docs/M12_BUSYBOX_GUEST.md).

### Guest LTP/POSIX on booted QEMU (M13)

```bash
git checkout cursor/m13-guest-ltp-posix-695c
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P13_GUEST_REGRESS
./tools/qemu_ltp_open_guest_smoke.sh             # LTP_OPEN_GUEST_OK (needs QEMU)
./tools/qemu_posix_io_guest_smoke.sh             # POSIX_IO_GUEST_OK (needs QEMU)
```

See [docs/M13_GUEST_LTP_POSIX.md](docs/M13_GUEST_LTP_POSIX.md).

### stat / poll / fstatat (M14)

```bash
git checkout cursor/m14-stat-poll-fstatat-695c
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P14_STAT_POLL
./tools/qemu_stat_guest_smoke.sh                 # STAT_GUEST_OK (needs QEMU)
./tools/qemu_poll_guest_smoke.sh                 # POLL_GUEST_OK (needs QEMU)
```

See [docs/M14_STAT_POLL.md](docs/M14_STAT_POLL.md).
