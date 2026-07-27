# M16 — Ring-3 fork/vfork + full ash regress on QEMU

M16 adds cooperative ring-3 scheduling for `fork`/`vfork`, pipe `dup2` wiring, blocking `wait4` and pipe reads, timer-backed `poll`, and runs the full BusyBox ash regress suite on booted QEMU.

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| Ring-3 scheduler | `ring3_sched.c`, `ring3_userret.S` | (kernel) |
| Pipe dup2 | `process.c`, `guest_io.c` | `P16_PIPE_DUP2` |
| Blocking wait/pipe | `process.c` | (kernel) |
| Timer poll | `process.c` `bfree_poll` | `P15_POLL_BLOCKING` |
| nanosleep stub | `syscall.c` | (syscall 35) |
| Full ash QEMU boot | `ash_regress_tramp.S` | `ASH_*_GUEST_OK` |
| Initramfs scripts | `guest/initramfs/ash_regress/*.sh` | `P16_ASH_FULL_STAGED` |

### Ash regress on QEMU (M16 full suite)

All five scripts run via `busybox ash -c` on boot:

- Pipeline (`echo | cat`) → `ASH_PIPE_GUEST_OK`
- Subshell isolation → `ASH_SUBSHELL_GUEST_OK`
- Command substitution → `ASH_CMDSUBST_GUEST_OK`
- Background `sleep 0 &; wait` → `ASH_BG_GUEST_OK`
- External `/bin/true` `/bin/false` → `ASH_EXTERNAL_GUEST_OK`

### Ring-3 fork/vfork

On each syscall entry the kernel saves the user `RCX`/`R11`/`RSP` frame per process. `fork` returns the child PID to the parent and defers `0` to the child on its next scheduled return. `vfork` runs the child immediately with return `0`; the parent resumes after `execve` or `exit`.

## Home PC quick start

```bash
git fetch origin
git checkout cursor/m16-ring3-fork-ash-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_ash_regress_guest_smoke.sh   # needs QEMU
```

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh              # P16_* + RESULT: ALL PASS
./tools/qemu_ash_regress_guest_smoke.sh
```

Rebuild ash regress kernel with `KERNEL_BOOT_CFLAGS=-DBFREE_PREFER_ASH_REGRESS_BOOT=1`.

## Residual (M17+)

- Per-process FD tables (today shared VFS fd state)
- True preemptive scheduling / timer ticks
- Migrate remaining host LTP/POSIX probes to guest
