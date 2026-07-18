#!/usr/bin/env bash
# CI/agent helper: rebuild + smoke in a loop until PASS or max attempts.
# Does not patch code — pair with Cursor Agent or manual fixes between runs.
#
#   bash tools/guest_desktop_debug_loop.sh 5
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAX="${1:-3}"

for (( attempt = 1; attempt <= MAX; attempt++ )); do
  echo ""
  echo "========== guest desktop attempt $attempt / $MAX =========="
  if BFREE_SMOKE_REBUILD=1 bash "$ROOT/tools/guest_desktop_smoke.sh"; then
    echo "[loop] PASS on attempt $attempt"
    exit 0
  fi
  code=$?
  echo "[loop] attempt $attempt failed (exit $code)"
  if (( attempt >= MAX )); then
    echo "[loop] giving up after $MAX attempts"
    exit "$code"
  fi
  echo "[loop] fix code, then re-run (or let Agent read serial + symbolize output)"
  exit "$code"
done
