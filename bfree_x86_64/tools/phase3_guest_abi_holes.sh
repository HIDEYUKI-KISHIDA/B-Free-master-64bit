#!/usr/bin/env bash
# Migrate curated host ABI/POSIX checks toward guest-visible gates (Cat4).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build/host-tests"

make -C "${ROOT}" -s "${BUILD}/test_p17_abi_holes"
"${BUILD}/test_p17_abi_holes"
# Host LTP/POSIX suites remain available; this script is the M17 bridge.
if [[ -x "${ROOT}/tools/phase3_guest_posix_regress.sh" ]]; then
  "${ROOT}/tools/phase3_guest_posix_regress.sh"
fi
echo "P17_GUEST_PROBE_MIGRATE: PASS"
