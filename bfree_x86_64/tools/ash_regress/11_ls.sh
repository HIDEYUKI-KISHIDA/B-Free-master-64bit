#!/bin/busybox ash
# ASH_LS — ls/getdents sees . and a known file.
set -eu
mkdir -p /tmp/ash_ls || exit 1
echo x > /tmp/ash_ls/item || exit 1
out=$(ls /tmp/ash_ls) || exit 1
echo "$out" | grep -q item || exit 1
out2=$(ls -a /tmp/ash_ls) || exit 1
echo "$out2" | grep -q '\.' || exit 1
rm /tmp/ash_ls/item || exit 1
rmdir /tmp/ash_ls || exit 1
echo "ASH_LS: PASS"
