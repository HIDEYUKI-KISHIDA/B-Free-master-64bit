# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-21  
**Full TODOLIST:** [POSIX_COMPAT_TODOLIST.md](./POSIX_COMPAT_TODOLIST.md)

## Done (recent)

- [x] COMPILE_OK restore
- [x] H02 / H01 / H06 kernel path (order 2→1→3)
- [x] link-fix: inet/unix/shm/pipe reclaim/timer tick (`5b2b711`)
- [x] H02 pipe smoke: `echo|cat` / `grep` pipe (no PF) (`02480c2`)

## Next (from TODOLIST)

| Pri | Item |
|-----|------|
| **P0** | phase3 ALL PASS ← **running** |
| **P1** | ptmx · ash fg · H01 fpstate/nested |
| **P1** | H17 / H26 方針 |
| **P2** | NOFORK=0 緑 |
| **P3–P4** | musl / 永続FS / LTP |

## Git

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
