# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-22 (polish E1–E3 + F1 scaffold)  
**Full TODOLIST:** [POSIX_COMPAT_TODOLIST.md](./POSIX_COMPAT_TODOLIST.md)  
**P4 scope:** [POSIX_PHASE7_AGREED_SCOPE.md](./POSIX_PHASE7_AGREED_SCOPE.md)

## Done

- [x] **P0** phase3 ALL PASS
- [x] **P1** residuals 方針
- [x] **P2** NOFORK=0 verified
- [x] **P3** musl `hello.elf` → `MUSL_HELLO_OK`
- [x] **P4** Phase7 **合意範囲** green（`tools/_p4_agreed_gate.sh`）
- [x] **E1** ash `FORK_BG` → `bfree_linux_fork`；`_p1_fg_smoke.sh` ALL PASS（jobs/fg）
- [x] **E2** futex 4-slot waiter queue + cleartid wake
- [x] **E3** nestable `preempt_disable` around clone/exit/futex（timer preempt は未）
- [x] **F1** `persist.img` scaffold（`tools/_f1_persist_img_scaffold.sh`）；block R/W は未

## P4 agreed vs deferred

| Agreed (green) | Deferred |
|----------------|----------|
| `/persist` RAM vfile | `persist.img` + block/ext2（img scaffold only） |
| pipe inet + `10.0.2/24` stub | real NIC |
| default ENOSYS residual | ENOSYS ゼロ化 |
| LTP gate SKIP + self-test | vendored LTP subset |

## Next

- F1 cont: ATA/AHCI sector R/W → tiny FAT/ext2 mount
- F2 UDP / real NIC path
- F3 curated LTP vendor
- H01 fpstate / full preemptive threads

## Git

- Work: `work/posix-holes-redo` @ `ae6ed61`
- Backup: `backup/p0-p4-complete-20260722` / tag `backup/p0-p4-green-20260722`
- Post-polish: `backup/polish-e1e3-f1-20260722` / tag `backup/polish-e1e3-f1-green-20260722`
