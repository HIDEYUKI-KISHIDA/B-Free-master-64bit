# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-22  
**Full TODOLIST:** [POSIX_COMPAT_TODOLIST.md](./POSIX_COMPAT_TODOLIST.md)

## Done (recent)

- [x] COMPILE_OK restore
- [x] H02 / H01 / H06 kernel path (order 2→1→3)
- [x] link-fix: inet/unix/shm/pipe reclaim/timer tick (`5b2b711`)
- [x] H02 pipe: AS-copy ref snap + parent resume (`02480c2`)
- [x] seq-fork: waitpid vs pipe yield (`6f1f6d7`)
- [x] execve basename + drop stale coop fd snaps (`d0b7139` / `37e3fa9`)
- [x] P8 restore: pread/select/flock/alarm/sockets + getpgid(121)/getsid(124)
- [x] PTY master↔slave I/O · vfile mmap · clock_getres(229) · dup3(292) · alarm↔nanosleep EINTR
- [x] **`tools/phase3_guest_auto.sh` → `RESULT: ALL PASS`** (2026-07-22)

## P0 phase3

| ブロック | 結果 |
|----------|------|
| core / M1 / B0 / B2 | PASS |
| P4–P7 | PASS |
| P8–P9 (`p8test` + shell markers) | PASS |

ログ: `bfree_x86_64/.cache/phase3_guest_report.txt`

## Next (from TODOLIST)

| Pri | Item |
|-----|------|
| **P1** | ash fg · H01 fpstate/nested · H17 / H26 方針 |
| **P2** | NOFORK=0 緑 |
| **P3–P4** | musl / 永続FS / LTP |

## Git

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
