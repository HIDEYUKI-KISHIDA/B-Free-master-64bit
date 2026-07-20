# POSIX/Linux holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-21  

## Remaining (summary)

| Bucket | Count | Meaning |
|--------|------:|---------|
| **REDO** | **17** | Must re-implement after wipe |
| **partial** | **2** | Started, not complete |
| **verify** | **11** | Likely in HEAD baseline; confirm with phase3 |
| **done** | **2** | Landed on this branch |
| **Total tracked** | **32** | H01–H32 |

**Active remaining to code:** **19** (= 17 REDO + 2 partial)

**Wipe residual note:** Pre-wipe ledger claimed all 32 filled; source was lost. This redo re-lands them with git backups.

## Done on this branch

- H04 SIGCHLD pending/mask — `4bc98f8`
- H14 AF_UNIX coop base — `5b322ea`
- H02 pipe coop — **partial** (yield/unix only; AS-copy CR3 still REDO via H02)

## REDO queue (phone checklist)

1. H01 rt_sigreturn+handlers (Blocker)
2. H02 pipe-concurrency AS-copy CR3 (Blocker) — partial
3. H03 per-process-fd (Blocker)
4. H06 background-jobs (Blocker)
5. H07 tty-stop-signals (High)
6. H09 select-pselect
7. H10 pread-pwrite
8. H12 SIGPIPE
9. H13 alarm-setitimer
10. H15 flock/fsync/… 
11. H17 sigaltstack-etc
12. H18 getsid
13. H19 mremap
14. H23 readv
15. H24 unix quality — partial
16. H26 CLONE_THREAD
17. H28 nofork-gate-meta (Blocker)
18. H30 pty-minimal (High)
19. H32 af-inet (Blocker)

## Git

- Work: `work/posix-holes-redo`
- Backup of broken recovery: `backup/syscall-wipe-recovery` (`649263a`)
- Ledger: `bfree_x86_64/tools/posix_holes_redo.tsv`

## Next

Continue REDO in order (Blockers first: H01 → H02 → H03 → H06 → H28 → H32 → …).
