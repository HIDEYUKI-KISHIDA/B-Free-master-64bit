#!/usr/bin/env bash
# Restore closest pre-wipe syscall.c from broken_replay + repair scripts.
set -eu
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program
export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
export GIT_AUTHOR_NAME='HIDEYUKI-KISHIDA'
export GIT_AUTHOR_EMAIL='h_kishida@kg8.so-net.ne.jp'
export GIT_COMMITTER_NAME='HIDEYUKI-KISHIDA'
export GIT_COMMITTER_EMAIL='h_kishida@kg8.so-net.ne.jp'

ROOT=bfree_x86_64
BAK=$ROOT/tools/_recovered_syscall/binary_bak

echo "[1] install broken_replay as working syscall.c"
cp -f "$BAK/syscall.c.broken_replay" "$ROOT/kernel/sysmain/syscall.c"
# keep prewipe .o for symbol compare
cp -f "$BAK/syscall.o.prewipe" "$ROOT/kernel/sysmain/syscall.o.prewipe"
wc -c "$ROOT/kernel/sysmain/syscall.c"

# Fix repair script ROOT path: they use parents[2] assuming tools/_recovered_syscall
# Path(__file__).parents[2] from tools/_recovered_syscall/_repair_syscall.py = bfree_x86_64 ✓

echo "[2] run repair passes"
for s in _repair_syscall.py _repair2_syscall.py _repair3_syscall.py; do
  f="$ROOT/tools/_recovered_syscall/$s"
  tr -d '\r' < "$f" > "$f.lf" && mv "$f.lf" "$f"
  echo "=== $s ==="
  python3 "$f" || echo "WARN $s rc=$?"
done

echo "[3] compile"
if make -C "$ROOT/kernel" sysmain/syscall.o 2>&1 | tee /tmp/restore_build.log | tail -40; then
  :
fi
errs=$(grep -c 'error:' /tmp/restore_build.log || true)
echo "error_count=$errs"
if [[ "$errs" -gt 0 ]]; then
  grep 'error:' /tmp/restore_build.log | sed 's/.*error: //' | sort | uniq -c | sort -rn | head -30
  exit 1
fi

echo "[4] marker vs prewipe intent"
for s in g_guest_fork_was_as_copy g_inet_socks g_bfree_exec_transfer_rip /dev/ptmx g_unix_socks bfree_coop_as_switch_to; do
  printf "%5d  %s\n" "$(grep -cF -- "$s" $ROOT/kernel/sysmain/syscall.c || true)" "$s"
done

echo "[5] commit restore checkpoint"
git add "$ROOT/kernel/sysmain/syscall.c"
git commit -m "restore: reinstate pre-wipe syscall.c via broken_replay+repair (resume 2-1-3)"
git log -1 --oneline

# Update status doc
cat > "$ROOT/docs/POSIX_HOLES_STATUS.md" <<'EOF'
# POSIX/Linux holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-21 (restore mode)

## Goal

Return to **pre-wipe “almost done”** state, then finish residuals in order **2 → 1 → 3**:

1. **H02** pipe-concurrency / AS-copy finish  
2. **H01** rt_sigreturn residuals (fpstate / nested CATCH)  
3. **H06** background-jobs ash fg UX  

## Residuals (pre-wipe ledger): 5

| # | Hole | Note |
|---|------|------|
| 1 | H01 | fpstate / nested CATCH |
| 2 | H02 | filled↑ AS-copy pipe wedge |
| 3 | H06 | ash fg UX polish |
| 4 | H17 | SS_AUTODISARM incomplete |
| 5 | H26 | no preemptive threads |

Active polish queue now: **H02 → H01 → H06** (user order). H17/H26 deferred.

## Restore

`syscall.c` restored from transcript-replay + repair scripts toward pre-wipe object.
EOF
git add "$ROOT/docs/POSIX_HOLES_STATUS.md"
git commit -m "docs: status — restore mode; resume residual order 2-1-3" || true

echo RESTORE_OK
