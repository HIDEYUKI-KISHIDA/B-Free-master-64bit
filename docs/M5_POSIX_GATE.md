# M5 POSIX gate

Host-testable POSIX surface for the final milestone (M5).

## Scope

| Area | Module | Marker |
|------|--------|--------|
| Device nodes | `devnode.c` | `P5_DEVNODE` |
| Mount | `mount.c` | `P5_MOUNT` |
| Credentials | `cred.c` | `P5_CRED` |
| AF_UNIX sockets | `net_unix.c` | `P5_NET_UNIX` |
| SysV shm | `ipc_shm.c` | `P5_IPC_SHM` |
| Syscall registry | `syscall_dispatch.c` | `P5_SYSCALL_GATE` |

## Gates

1. `cd bfree_x86_64 && ./tools/phase3_guest_auto.sh` — full M1–M5 host regression
2. `cd bfree_x86_64 && ./tools/phase3_guest_posix.sh` — M5-only P5 markers

## Residual (intentional)

- Full LTP / POSIX conformance suite on booted guest — out of host-test scope
- musl static binary on booted guest — deferred to boot/trap integration (M4 residual)
- Network beyond AF_UNIX (INET, etc.)
- Full SysV IPC (msg, sem)

Unimplemented syscalls return `ENOSYS`; the registry tracks which numbers are implemented vs residual.
