# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-21T03:17:36+09:00  
**Mode:** Residuals **H02 → H01 → H06** complete (kernel); polish leftovers noted

## Progress

- [x] **COMPILE_OK** — `sysmain/syscall.o` errors=0
- [x] **H02** AS-copy / pipe concurrency — coop CR3 switch, parent-first fork, yield_done, EAGAIN via live_count, FORK_PARENT ignores exec_cr3
- [x] **H01** sigframe / handler path — `bfree_rt_sigframe_t`, `g_bfree_sig_saved_*`, CATCH deliver + `rt_sigreturn`
- [x] **H06** job control / fg path — setpgid/getpgid/setsid, TIOCSPGRP/TIOCGPGRP, kill stop/cont, soft TTIN

## Still residual

- **H01:** no full glibc **fpstate**; no **nested CATCH**
- **H06 ash fg UX:** kernel tcsetpgrp/setpgid wired; BusyBox ash job-table/`fg` UX may still need a userspace patch (out of kernel scope)
- **ptmx:** re-apply `_patch_pty_stage2.py` after FD-base reconcile vs inet `0x3B00`
- Deferred holes: H17/H26

## Markers

as_copy yes · inet yes · exec_transfer yes · ptmx no

## Git tips

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
