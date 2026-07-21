# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-22 (P1)  
**Full TODOLIST:** [POSIX_COMPAT_TODOLIST.md](./POSIX_COMPAT_TODOLIST.md)

## Done (recent)

- [x] **P0** `phase3` → `RESULT: ALL PASS` (`a94e501`)
- [x] **T-P1-3** ptmx master↔slave I/O
- [x] **T-P1-4** H17: `sigaltstack` + `SA_ONSTACK` 復元；**SS_AUTODISARM = soft accept / not enforced**
- [x] **T-P1-5** H26: `CLONE_THREAD|CLONE_VM` → serial coop thread gate；**preemptive → deferred P3**
- [x] **T-P1-1** H01: kernel CATCH path 維持；**fpstate / nested CATCH → deferred polish**
- [~] **T-P1-2** H06: kernel jobctl 完了；ash `fg` は **`sh: fg: No current job`**（ユーザー空間 residual；パッチは別）

## P1 residuals（方針）

| ID | 状態 |
|----|------|
| H01 fpstate / nested CATCH | **deferred**（パニック無しの現状維持；本実装は P3 polish） |
| H06 ash `fg` | カーネル完了；**ash job table/fg UX residual**（smoke: no panic; `fg` 未成功） |
| H17 SS_AUTODISARM | **soft residual**（フラグ受理・自動 disarm なし） |
| H26 preemptive threads | **deferred→P3**（`T-P3-1`） |

## Next

| Pri | Item |
|-----|------|
| **P2** | `BFREE_NOFORK_ALL=0` 緑 |
| **P3** | musl / CLONE_THREAD 深化 / fpstate |

## Git

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
