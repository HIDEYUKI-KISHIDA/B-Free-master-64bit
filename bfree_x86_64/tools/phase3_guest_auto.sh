#!/usr/bin/env bash
# Guest regression gate (Phase 3 base + per-milestone markers).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build/host-tests"
FAIL=0

run_test() {
  local name="$1"
  echo "== phase3_guest_auto: ${name} =="
  if ! "${BUILD_DIR}/${name}"; then
    FAIL=1
  fi
}

mkdir -p "${BUILD_DIR}"

echo "== phase3_guest_auto: build host tests =="
make -C "${ROOT}" -s host-tests

run_test test_p4_dirent_ofd
run_test test_p4_unlink_open
run_test test_p4_openat
run_test test_p4_block_fs

if [[ "${FAIL}" -ne 0 ]]; then
  echo "RESULT: FAIL"
  exit 1
fi

echo "RESULT: ALL PASS"
