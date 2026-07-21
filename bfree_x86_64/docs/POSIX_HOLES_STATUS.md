# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-21 (P0 部分緑)  
**Full TODOLIST:** [POSIX_COMPAT_TODOLIST.md](./POSIX_COMPAT_TODOLIST.md)

## Done (recent)

- [x] COMPILE_OK restore
- [x] H02 / H01 / H06 kernel path (order 2→1→3)
- [x] link-fix: inet/unix/shm/pipe reclaim/timer tick (`5b2b711`)
- [x] H02 pipe: AS-copy ref snap + parent resume (`02480c2`)
- [x] seq-fork: waitpid vs pipe yield (`6f1f6d7`)
- [x] **phase3 前半緑** — core / M1 / B0 / B2 / P4–P6 ほぼ PASS（`6f1f6d7` 時点）

## P0 phase3 残り（`RESULT: SOME FAIL`）

| ブロック | 内容 |
|----------|------|
| P7 | hardlink inode/share · `kill -TERM $$` → 143 |
| P8–P9 | `/var` 書き込み · `/home` · **`/dev/ptmx`**（ptmx 未復元→PF）· p8test 未到達 |

ログ: `bfree_x86_64/.cache/phase3_guest_report.txt`

## Next (from TODOLIST)

| Pri | Item |
|-----|------|
| **P0** | 上記 P7 + ptmx + p8test で **ALL PASS** |
| **P1** | ptmx · ash fg · H01 fpstate/nested |
| **P1** | H17 / H26 方針 |
| **P2** | NOFORK=0 緑 |
| **P3–P4** | musl / 永続FS / LTP |

## Git

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
