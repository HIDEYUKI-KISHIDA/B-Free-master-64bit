# ptmx recovery

PTY/ptmx was applied via `tools/_patch_pty_stage2.py`, not a single transcript dump of final C.

Re-apply: `py -3 tools/_patch_pty_stage2.py` on HEAD `syscall.c`.

**FD base conflict:** that script uses `BFREE_PTY_MASTER_BASE 0x3a00`, while recovered `inet.c` uses `BFREE_INET_FD_BASE 0x3A00`. Reconcile before merging both.
