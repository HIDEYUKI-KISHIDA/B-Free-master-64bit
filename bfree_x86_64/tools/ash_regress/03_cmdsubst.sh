#!/bin/busybox ash
# ASH_CMDSUBST — $(cmd) command substitution via pipe/fork.
set -eu
out=$(echo hello)
[ "$out" = "hello" ] || { echo "ASH_CMDSUBST: FAIL (got '$out')"; exit 1; }
echo "ASH_CMDSUBST: PASS"
