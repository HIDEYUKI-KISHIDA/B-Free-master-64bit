#!/bin/busybox ash
# ASH_SUBSHELL — ( cmd ) runs in subshell; parent vars unchanged.
set -eu
marker=parent
( marker=subshell )
[ "$marker" = "parent" ] || { echo "ASH_SUBSHELL: FAIL"; exit 1; }
echo "ASH_SUBSHELL: PASS"
