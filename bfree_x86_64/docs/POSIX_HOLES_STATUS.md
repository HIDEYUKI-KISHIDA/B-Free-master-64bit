# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-22 (P4)  
**Full TODOLIST:** [POSIX_COMPAT_TODOLIST.md](./POSIX_COMPAT_TODOLIST.md)  
**P4 scope:** [POSIX_PHASE7_AGREED_SCOPE.md](./POSIX_PHASE7_AGREED_SCOPE.md)

## Done

- [x] **P0** phase3 ALL PASS
- [x] **P1** residuals 方針
- [x] **P2** NOFORK=0 verified
- [x] **P3** musl `hello.elf` → `MUSL_HELLO_OK`
- [x] **P4** Phase7 **合意範囲** green（`tools/_p4_agreed_gate.sh`）

## P4 agreed vs deferred

| Agreed (green) | Deferred |
|----------------|----------|
| `/persist` RAM vfile | `persist.img` + block/ext2 |
| pipe inet + `10.0.2/24` stub | real NIC |
| default ENOSYS residual | ENOSYS ゼロ化 |
| LTP gate SKIP + self-test | vendored LTP subset |

## Next (beyond TODOLIST P0–P4)

- polish: ash `fg`、pthread 並行、futex waiter キュー、fpstate
- Phase7 full: 永続ブロック FS / 本ネット / LTP claim

## Git

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
