#!/bin/busybox ash
# ASH_BG — background job via fork; parent waits with wait builtin.
set -eu
sleep 0 &
wait
echo "ASH_BG: PASS"
