#!/usr/bin/env bash
# P4 agreed Phase-7 gate (see docs/POSIX_PHASE7_AGREED_SCOPE.md).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CACHE="$ROOT/.cache"
OUT="$CACHE/p4_gate_status.txt"
mkdir -p "$CACHE"
fail=0

{
  echo "=== P4 agreed gate $(date -Iseconds) ==="

  # T-P4-1 /persist note
  if [[ -f tools/sf01_persistent_fs_note.md ]]; then
    echo "PASS T-P4-1 sf01_persistent_fs_note.md present"
  else
    echo "FAIL T-P4-1 missing sf01 note"
    fail=1
  fi
  if grep -q 'PASS p8_persist' .cache/phase3_guest_report.txt 2>/dev/null; then
    echo "PASS T-P4-1 phase3 p8_persist (historical ALL PASS log)"
  else
    echo "WARN T-P4-1 no PASS p8_persist in report (re-run phase3 if needed)"
  fi

  # T-P4-2 net stub
  if grep -q 'PASS p8_slirp\|PASS p8_inet' .cache/phase3_guest_report.txt 2>/dev/null; then
    echo "PASS T-P4-2 phase3 p8_inet/p8_slirp (historical ALL PASS log)"
  else
    echo "WARN T-P4-2 no PASS p8_inet/slirp in report"
  fi

  # T-P4-3 ENOSYS default + inventory
  if grep -q 'BFREE_LINUX_SYSCALL_UNHANDLED' kernel/sysmain/syscall.c; then
    echo "PASS T-P4-3 UNHANDLED→ENOSYS path present"
  else
    echo "FAIL T-P4-3 missing UNHANDLED sentinel"
    fail=1
  fi
  n_cases=$(grep -cE '^\s+case [0-9]+:' kernel/sysmain/syscall.c || true)
  echo "INFO T-P4-3 linux case arms ≈ $n_cases (intentional residual = default ENOSYS)"

  # T-P4-4 LTP gate
  if bash tools/ltp_subset_gate.sh >/tmp/p4_ltp_out.txt 2>&1; then
    echo "PASS T-P4-4 ltp_subset_gate.sh exit 0"
    if grep -q 'STATUS: SKIP\|STATUS: PRESENT' .cache/ltp_gate_status.txt 2>/dev/null; then
      echo "PASS T-P4-4 status file: $(head -1 .cache/ltp_gate_status.txt)"
    fi
  else
    echo "FAIL T-P4-4 ltp_subset_gate.sh"
    fail=1
  fi

  if [[ -f docs/POSIX_PHASE7_AGREED_SCOPE.md ]]; then
    echo "PASS agreed-scope doc present"
  else
    echo "FAIL missing POSIX_PHASE7_AGREED_SCOPE.md"
    fail=1
  fi

  if [[ "$fail" -eq 0 ]]; then
    echo "RESULT: P4 AGREED SCOPE GREEN"
  else
    echo "RESULT: P4 AGREED SCOPE FAIL"
  fi
} | tee "$OUT"

exit "$fail"
