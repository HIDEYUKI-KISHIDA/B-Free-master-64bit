#!/bin/busybox ash
# ASH_PIPE — pipeline via kernel pipe + fork (not inproc memcpy).
set -eu
out=$(echo pipe-data | cat)
[ "$out" = "pipe-data" ] || { echo "ASH_PIPE: FAIL (got '$out')"; exit 1; }
echo "ASH_PIPE: PASS"
