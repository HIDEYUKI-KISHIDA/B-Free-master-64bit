#!/usr/bin/env bash
# Start Phase 3 automation in background (for when you're away).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p "$ROOT/.cache"
LOG="$ROOT/.cache/phase3_guest_auto.log"
nohup bash "$ROOT/tools/phase3_guest_auto.sh" >>"$LOG" 2>&1 &
echo "$!" > "$ROOT/.cache/phase3_guest_auto.pid"
echo "[phase3] started PID $(cat "$ROOT/.cache/phase3_guest_auto.pid")"
echo "[phase3] log: $LOG"
echo "[phase3] report: $ROOT/.cache/phase3_guest_report.txt"
echo "  tail -f $LOG"
echo "  tail -f $ROOT/.cache/phase3_guest_report.txt"
