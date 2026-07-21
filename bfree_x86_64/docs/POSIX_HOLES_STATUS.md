# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-22 (P2)  
**Full TODOLIST:** [POSIX_COMPAT_TODOLIST.md](./POSIX_COMPAT_TODOLIST.md)

## Done (recent)

- [x] **P0** `phase3` → `RESULT: ALL PASS` (`a94e501`)
- [x] **P1** H17/H26 gate + H01/H06 residual 方針 (`f0bab5c`)
- [x] **P2** `BFREE_NOFORK_ALL=0` + `KEEP_INPROC=0` → **phase3 ALL PASS**（verified）
  - ash: `APPLET_IS_NOFORK` · seq-fork · NOEXEC→execve · **no** inproc/bg-inline
  - 検証: `tools/_p2_nofork_phase3.sh`

## P1 residuals（据え置き）

| ID | 状態 |
|----|------|
| H01 fpstate / nested CATCH | deferred polish |
| H06 ash `fg` UX | userspace residual（`No current job`） |
| H17 SS_AUTODISARM | soft accept |
| H26 preemptive threads | →P3 |

## Next

| Pri | Item |
|-----|------|
| **P3** | musl 静的 / CLONE_THREAD 深化 / futex / fpstate |
| **P4** | 永続FS / 本ネット / LTP |

## Git

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
