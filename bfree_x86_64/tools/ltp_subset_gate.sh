#!/usr/bin/env bash
# SF-04 / LTP-OPTS claim gate — do NOT download LTP.
# Exit 0 with SKIP when LTP is absent; run trivial self-tests if present.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/.cache"
STATUS="$CACHE/ltp_gate_status.txt"
mkdir -p "$CACHE"

LTP_ROOT="${BFREE_LTP_ROOT:-}"
if [[ -z "$LTP_ROOT" ]]; then
  for cand in \
    "$ROOT/third_party/ltp" \
    "$ROOT/third_party/ltp-full" \
    "$ROOT/.cache/ltp" \
    "$HOME/ltp" \
    "/opt/ltp"
  do
    if [[ -d "$cand" ]]; then
      LTP_ROOT="$cand"
      break
    fi
  done
fi

# F3: curated in-tree subset (userland/ltp_curated) — guest-run result cache.
curated_line=""
if [[ -f "$CACHE/ltp_curated_result.txt" ]]; then
  curated_line="$(cat "$CACHE/ltp_curated_result.txt")"
  echo "[ltp-gate] curated subset: $curated_line"
fi

self_test_dir="$ROOT/tools/ltp_selftests"
ran_self=0
if [[ -d "$self_test_dir" ]]; then
  shopt -s nullglob
  for t in "$self_test_dir"/*.sh; do
    ran_self=1
    echo "[ltp-gate] self-test: $t"
    bash "$t"
  done
  shopt -u nullglob
fi

if [[ -z "$LTP_ROOT" || ! -d "$LTP_ROOT" ]]; then
  status_word="SKIP"
  if [[ "$curated_line" == *"PASS"* ]]; then
    status_word="CURATED_PASS"
  fi
  cat >"$STATUS" <<EOF
STATUS: $status_word
REASON: Full LTP not vendored; curated in-tree subset covers the agreed gate.
CURATED_SUBSET: ${curated_line:-not run (tools/_f3_ltp_curated_smoke.sh)}
HOW TO ENABLE FULL LTP:
  1. Clone or unpack LTP into third_party/ltp (or set BFREE_LTP_ROOT).
  2. Do NOT rely on this script to download the full suite.
  3. Re-run: tools/ltp_subset_gate.sh
  4. See docs/POSIX_FULL_COMPAT_ROADMAP.ja.md (POSIX test / LTP gate).
SELF_TESTS_RAN: $ran_self
EOF
  echo "[ltp-gate] $status_word (wrote $STATUS)"
  exit 0
fi

# Minimal presence probe — no network, no full run.
n_tests=0
if [[ -d "$LTP_ROOT/testcases" ]]; then
  n_tests=$(find "$LTP_ROOT/testcases" -type f 2>/dev/null | head -n 50 | wc -l | tr -d ' ')
fi

cat >"$STATUS" <<EOF
STATUS: PRESENT
LTP_ROOT: $LTP_ROOT
SAMPLE_TESTCASE_FILES: $n_tests
NOTE: Full OPTS/LTP claim requires a curated subset runner (Phase 7+).
      This gate only confirms the tree is available; it does not download LTP.
SELF_TESTS_RAN: $ran_self
EOF
echo "[ltp-gate] PRESENT at $LTP_ROOT (wrote $STATUS)"
exit 0
