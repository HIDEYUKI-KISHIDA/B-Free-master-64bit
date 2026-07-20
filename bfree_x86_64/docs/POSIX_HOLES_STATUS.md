# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-21T03:16:12+09:00  
**Mode:** Residuals **H02 → H01 → H06** (compile stays green)

## Progress

- [x] **COMPILE_OK** — `sysmain/syscall.o` errors=0
- [x] **H02** AS-copy / pipe concurrency — coop CR3 switch, parent-first fork, yield_done, EAGAIN via live_count, FORK_PARENT ignores exec_cr3
- [x] **H01** sigframe / handler path — `bfree_rt_sigframe_t`, `g_bfree_sig_saved_*`, CATCH deliver + `rt_sigreturn`; **residual: no glibc fpstate, no nested CATCH**
- [ ] **H06** job control / fg path

## Now

- Next: **H06** (tty_pgrp / setpgid / stop-cont; ash fg UX may need busybox note)
- Markers: as_copy yes, inet yes, exec_transfer yes; ptmx deferred

## Git tips

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
