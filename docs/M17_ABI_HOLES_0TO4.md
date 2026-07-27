# M17 — Linux ABI holes categories 0–4

Fill the ABI gap inventory in `docs/LINUX_ABI_HOLES.ja.md`.

## Scope

| Cat | Focus | Marker |
|----:|-------|--------|
| 0 | Fix wrong Linux NR wiring (`chmod`/`fchmod`/`prctl`/`cap*`/`prlimit64`) | `P17_ABI_HOLES` |
| 1 | High-priority BusyBox/musl CLI `ENOSYS` | same |
| 2 | Strengthen thin stubs (signals, ioctl, nanosleep, listen/accept, clone/futex) | same |
| 3 | Medium POSIX (SysV sem/msg, epoll, cred groups, arch_prctl, …) | same |
| 4 | Per-process FD tables, sched tick / preempt yield, guest-probe path | same |

## Key paths

- `kernel/sysmain/syscall.c` / `syscall_dispatch.c`
- `kernel/sysmain/fs_ofd.c` (rename/link/symlink/chmod/…)
- `kernel/sysmain/net_unix.c` (listen/accept/…)
- `kernel/sysmain/ipc_sysv.c`, `epoll.c`
- `kernel/sysmain/process.c` (per-proc FD + `bfree_sched_tick`)

## Counts

| Metric | M16 | M17 |
|--------|----:|----:|
| Registered | 64 | 190 |
| `ENOSYS` in `0..399` | 336 | 210 |

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh
./build/host-tests/test_p17_abi_holes
```

## Residual

- Long-tail `ENOSYS` (~210 in 0..399)
- INET, io_uring, namespaces (non-goals)
- Full upstream LTP on guest
