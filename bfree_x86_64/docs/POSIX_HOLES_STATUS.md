# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-21T03:07:00+09:00  
**Mode:** **COMPILE_OK** → residuals **H02 → H01 → H06**

## Now

- `make -C bfree_x86_64/kernel sysmain/syscall.o` → **errors=0** (COMPILE_OK)
- Soft restore glue in place for missing unix/coop/sig/inet helpers; full H02 yield bodies still soft
- **Next:** crush **H02** (pipe / AS-copy finish), then **H01** (sigframe), then **H06** (ash fg UX)
- Pre-wipe residuals total were **5**; active polish queue is **3** (H02/H01/H06). H17/H26 deferred.
- Markers: `as_copy` yes, `inet` yes, `exec_transfer` yes, `ptmx` not yet (re-apply `_patch_pty_stage2.py` after FD-base reconcile vs inet `0x3B00`)

## Order (user)

1. **H02** pipe / AS-copy finish  
2. **H01** sigframe residuals  
3. **H06** ash fg UX  

## Git tips

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
