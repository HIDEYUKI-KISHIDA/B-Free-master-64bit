# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-22 (P3)  
**Full TODOLIST:** [POSIX_COMPAT_TODOLIST.md](./POSIX_COMPAT_TODOLIST.md)

## Done (recent)

- [x] **P0** phase3 ALL PASS (`a94e501`)
- [x] **P1** residuals 方針 (`f0bab5c`)
- [x] **P2** NOFORK=0 verified (`97c577e`)
- [x] **P3** musl 静的 `hello.elf` ゲスト実行 → **`MUSL_HELLO_OK`**
  - Multiboot module + execve basename `hello.elf`
  - link `-Ttext-segment=0x500000`
  - smoke: `tools/_p3_musl_hello_smoke.sh`
  - 「小 CLI」= musl busybox（P2 緑）

## P3 詳細

| ID | 状態 |
|----|------|
| T-P3-3 musl hello | ✅ `MUSL_HELLO_OK` |
| T-P3-2 futex | ✅ timed WAIT→ETIMEDOUT；untimed clear = Qt soft residual |
| T-P3-1 CLONE_THREAD | serial coop gate 維持；**preemptive deferred** |
| T-P3-4 musl jobctl | カーネル setpgid/TTY 済み；ash/`fg` UX residual（P1 と同） |

## Next

| Pri | Item |
|-----|------|
| **P4** | 永続FS / 本ネット / LTP |
| polish | pthread 並行・waiter キュー・fpstate |

## Git

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
