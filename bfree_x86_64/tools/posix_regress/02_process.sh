#!/usr/bin/env bash
# POSIX_PROC — fork, wait, vfork/exec.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"${ROOT}/build/host-tests/test_p4_fork"
"${ROOT}/build/host-tests/test_p4_waitid"
"${ROOT}/build/host-tests/test_p4_vfork_exec"
echo "POSIX_PROC: PASS"
