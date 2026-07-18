#!/usr/bin/env bash
# LTP_TRAP — trap setup + trap payload exec.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"${ROOT}/build/host-tests/test_p7_trap_setup"
"${ROOT}/build/host-tests/test_p7_trap_payload"
echo "LTP_TRAP: PASS"
