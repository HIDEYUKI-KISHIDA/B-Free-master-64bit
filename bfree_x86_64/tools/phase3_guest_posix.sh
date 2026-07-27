#!/usr/bin/env bash
# M5 POSIX host regression gate (P5 markers).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build/host-tests"
FAIL=0

run_test() {
  local name="$1"
  echo "== phase3_guest_posix: ${name} =="
  if ! "${BUILD_DIR}/${name}"; then
    FAIL=1
  fi
}

mkdir -p "${BUILD_DIR}"
make -C "${ROOT}" -s host-tests

run_test test_p5_devnode
run_test test_p5_mount
run_test test_p5_cred
run_test test_p5_net_unix
run_test test_p5_ipc_shm
run_test test_p5_syscall_gate

if [[ "${FAIL}" -ne 0 ]]; then
  echo "RESULT: FAIL"
  exit 1
fi

echo "RESULT: ALL PASS"
