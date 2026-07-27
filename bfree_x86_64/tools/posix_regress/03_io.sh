#!/usr/bin/env bash
# POSIX_IO — pipe and dup2 I/O.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"${ROOT}/build/host-tests/test_p4_pipe_signal"
"${ROOT}/build/host-tests/test_p4_rw_dup2"
echo "POSIX_IO: PASS"
