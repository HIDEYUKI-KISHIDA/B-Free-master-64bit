# POSIX holes — mobile status

**Updated:** 2026-07-21 02:52 JST  
**Branch:** `work/posix-holes-redo`  
**Build:** `COMPILE_FAIL errors≈59`

## Mission (user order)

Restore pre-wipe “almost done”, then crush residuals **2 → 1 → 3**:

| Step | Hole | Status |
|------|------|--------|
| **2** | H02 pipe / AS-copy finish | pending (needs compile green) |
| **1** | H01 sigframe residuals | pending |
| **3** | H06 ash fg UX | pending |

Pre-wipe residual pool was **5** (H01, H02, H06, H17, H26). Active queue = first three.

## Restore markers in syscall.c

| Marker | Present |
|--------|---------|
| as_copy | True |
| inet | True |
| exec_transfer_rip | True |
| unix socks | True |
| /dev/ptmx | False |

## Links

- This file (refresh): branch `work/posix-holes-redo` → `bfree_x86_64/docs/POSIX_HOLES_STATUS.md`
- Backup: `backup/syscall-wipe-recovery`
