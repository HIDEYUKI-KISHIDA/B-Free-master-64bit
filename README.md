# B-Free-master-64bit

64-bit open-source OS inheriting BTRON philosophy, with a Linux/POSIX guest compatibility track.

## POSIX compatibility roadmap

See [docs/POSIX_FULL_COMPAT_ROADMAP.ja.md](docs/POSIX_FULL_COMPAT_ROADMAP.ja.md) for milestone tracking (M0–M5).

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
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # P4_RW_DUP2, P4_BRK_MMAP, P4_ELF_LOAD, P4_CLONE_FUTEX, P4_TTY_JOB
```
