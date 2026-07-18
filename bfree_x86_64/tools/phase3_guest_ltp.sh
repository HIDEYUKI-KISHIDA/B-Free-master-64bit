#!/usr/bin/env bash
# Minimal LTP-style gate via syscall invoke (M7).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build/host-tests"
REGRESS="${ROOT}/tools/ltp_regress"
FAIL=0

run_case() {
  local script="$1"
  echo "== phase3_guest_ltp: $(basename "${script}") =="
  if ! bash "${script}"; then
    FAIL=1
  fi
}

mkdir -p "${BUILD}"
make -C "${ROOT}" -s host-tests

for script in "${REGRESS}"/*.sh; do
  [[ -f "${script}" ]] || continue
  run_case "${script}"
done

if [[ "${FAIL}" -ne 0 ]]; then
  echo "LTP_REGRESS: FAIL"
  exit 1
fi

echo "LTP_REGRESS: ALL PASS"
