# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-21T03:04:26+09:00  
**Mode:** RESTORE → then residuals **2 → 1 → 3** (H02 → H01 → H06)

## Now

- Restoring pre-wipe `syscall.c` (compile errors remaining: **40**)
- After COMPILE_OK: crush H02, then H01, then H06
- Pre-wipe residuals total were **5**; active polish queue is **3** (H02/H01/H06). H17/H26 deferred.

## Order (user)

1. **H02** pipe / AS-copy finish  
2. **H01** sigframe residuals  
3. **H06** ash fg UX  

## Git tips

- Work: `work/posix-holes-redo`
- Backup: `backup/syscall-wipe-recovery`
