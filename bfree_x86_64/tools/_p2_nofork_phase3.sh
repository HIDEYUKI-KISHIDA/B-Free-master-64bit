#!/usr/bin/env bash
# P2 gate: rebuild busybox with NOFORK=0 + KEEP_INPROC=0, then phase3.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
export BFREE_NOFORK_ALL=0
export BFREE_ASH_KEEP_INPROC_PIPE=0
export BFREE_ASH_INLINE_PATCHES=0

LOG=/tmp/p2_rebuild.log
: > "$LOG"
echo "[p2] rebuild busybox NOFORK_ALL=0 KEEP_INPROC=0" | tee -a "$LOG"
if ! bash tools/build_guest_busybox.sh >>"$LOG" 2>&1; then
  echo "FAIL busybox rebuild" | tee -a "$LOG"
  tail -40 "$LOG"
  exit 1
fi

ASH="$ROOT/.cache/busybox-src/shell/ash.c"
echo "[p2] verify ash gates" | tee -a "$LOG"
grep -q 'APPLET_IS_NOFORK(applet_no)' "$ASH" || { echo "FAIL no APPLET_IS_NOFORK"; exit 1; }
grep -q 'B-Free: nofork all' "$ASH" && { echo "FAIL still nofork-all"; exit 1; }
grep -q 'B-Free: force execve for NOEXEC' "$ASH" || { echo "FAIL no NOEXEC->execve"; exit 1; }
grep -q 'B-Free: seq fork pipe\|B-Free: seq-fork' "$ASH" || { echo "FAIL no seq-fork"; exit 1; }
grep -q 'B-Free: inproc pipe' "$ASH" && { echo "FAIL inproc still present"; exit 1; }
echo "PASS ash_gates" | tee -a "$LOG"

echo "[p2] phase3 (reuse kernel/init, fresh busybox via install paths)" | tee -a "$LOG"
BFREE_PHASE3_REUSE_BUILD=1 bash tools/phase3_guest_auto.sh >>"$LOG" 2>&1 || true
tail -30 "$LOG"
grep -E 'RESULT:|FAIL |PASS ash' "$LOG" | tail -20
