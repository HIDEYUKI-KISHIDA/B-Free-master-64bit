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

# M1 filesystem
run_test test_p4_dirent_ofd
run_test test_p4_unlink_open
run_test test_p4_openat
run_test test_p4_block_fs

# M2 process
run_test test_p4_vfork_exec
run_test test_p4_waitid
run_test test_p4_fork
run_test test_p4_pipe_signal

# M3 normal CLI
run_test test_p4_shell_cli

if [[ "${FAIL}" -ne 0 ]]; then
  echo "RESULT: FAIL"
  exit 1
fi

echo "RESULT: ALL PASS"
