#!/usr/bin/env bash
# Guest regression gate (Phase 3 base + per-milestone markers).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build/host-tests"
FAIL=0

mkdir -p "${BUILD_DIR}"

echo "== phase3_guest_auto: build host tests =="
make -C "${ROOT}" -s host-tests

echo "== phase3_guest_auto: P4_DIRENT_OFD =="
if ! "${BUILD_DIR}/test_p4_dirent_ofd"; then
  FAIL=1
fi

if [[ "${FAIL}" -ne 0 ]]; then
  echo "RESULT: FAIL"
  exit 1
fi

echo "RESULT: ALL PASS"
