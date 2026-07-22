# M3 — BusyBox patch rollback plan

This document records the **removed guest hacks** and the **M2/M3 kernel primitives** that replace them. Apply when integrating a local BusyBox tree with `bfree_x86_64`.

## Policy

| Old hack | Problem | Replacement |
|----------|---------|-------------|
| **inproc pipe** | ash ran both ends of `\|` in the same task with memcpy | `pipe` + `fork` + `execve` per stage (`bfree_shell_pipeline`) |
| **bg-inline** | background `&` ran synchronously in parent | `fork` + return pid; parent continues; `wait4`/`waitid` later (`bfree_shell_bg`) |
| **NOFORK-all** | every applet ran inline in shell address space | upstream NOFORK only for safe builtins; others use `vfork`/`execve` (`bfree_shell_external`) |

## BusyBox changes to revert

When `third_party/busybox/` is vendored:

1. **Remove NOFORK-all override** in `shell/ash.c` / applet tables — restore upstream `APPLET_NOFORK` markings only where upstream marks them.
2. **Remove inproc pipe runner** — any custom `run_pipe_inproc()` or equivalent; ash must call `pipe(2)`, `fork(2)`, `dup2(2)`, `execve(2)`.
3. **Remove bg-inline** — background jobs must `fork` and record job table entry; no synchronous inline execution of `&` commands.
4. **Keep** `/bin/sh` → `busybox ash` symlink in rootfs.

## Build integration

```bash
# After vendoring BusyBox:
export BUSYBOX_SRC=/path/to/busybox
export CROSS_COMPILE=x86_64-linux-gnu-
./bfree_x86_64/tools/build_guest_busybox.sh
```

The script validates that patch-rollback markers are absent and produces `guest/rootfs/bin/busybox` using `configs/busybox_posix.config` (expanded static guest profile, ~56 applet symlinks). Override with `BUSYBOX_CONFIG=configs/busybox_m3.config` for the original minimal ash-only build.

## Host verification (in-repo, no BusyBox tree required)

```bash
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh
```

M3 markers:

| Marker | Behavior |
|--------|----------|
| `P4_PIPE_FORK` | Pipeline via forked writer/reader + kernel pipe |
| `P4_SUBSHELL` | `( cmd )` — child AS isolated from parent |
| `P4_CMDSUBST` | `$(cmd)` — child stdout via pipe |
| `P4_BG_JOB` | `cmd &` — fork without inline wait |
| `P4_EXTERNAL` | `/bin/true` via vfork+execve |
| `P4_SHELL_CLI` | Combined M3 gate |

## Guest ash regression (local tree)

```bash
./bfree_x86_64/tools/build_guest_busybox.sh   # fetch upstream BusyBox, build rootfs
./bfree_x86_64/tools/phase3_guest_ash.sh      # ASH_PIPE, ASH_SUBSHELL, ASH_CMDSUBST, ASH_BG, ASH_EXTERNAL
```

Or run the full gate (host P4 + guest ash):

```bash
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh
```

Scripts under `bfree_x86_64/tools/ash_regress/` mirror the host `P4_*` cases using upstream ash (no inproc pipe / bg-inline / NOFORK-all).

## Syscall prerequisites for real BusyBox

Minimum beyond M2 for ash without hacks:

- `read`, `write`, `close`, `dup`, `dup2`
- `chdir`, `getcwd` (or fixed root)
- `fcntl` (FD_CLOEXEC)
- `brk` or `mmap` for heap
- Real ELF `execve` from block FS (M4 track)

Until ELF load lands, host `shell_cli.c` exercises the same orchestration with registered applets.
