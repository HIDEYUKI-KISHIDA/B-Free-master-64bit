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
| musl static ELF | `elf_host_run.c` | `P4_MUSL_LOAD` / `P4_MUSL_EXEC` |

## Gates

1. `cd bfree_x86_64 && ./tools/phase3_guest_auto.sh` — full M1–M5 host regression
2. `cd bfree_x86_64 && ./tools/phase3_guest_posix.sh` — M5-only P5 markers
3. `cd bfree_x86_64 && ./tools/phase3_guest_posix_regress.sh` — curated POSIX regress (`posix_regress/`)

## posix_regress/

| Script | Coverage |
|--------|----------|
| `01_fs.sh` | devnodes, mount |
| `02_process.sh` | fork, wait, vfork/exec |
| `03_io.sh` | pipe, dup2 |
| `04_ipc.sh` | AF_UNIX, SysV shm |
| `05_cred.sh` | uid/gid, syscall registry |
| `06_static_elf.sh` | musl-style static ET_EXEC load + exec |

## Residual (post-boot)

- Full upstream LTP on booted guest (requires syscall trap + QEMU harness)
- INET sockets, full SysV IPC (msg, sem)
- musl exec via in-kernel syscall trap (host shim uses fork + mprotect today)

Unimplemented syscalls return `ENOSYS`; the registry tracks which numbers are implemented vs residual.
