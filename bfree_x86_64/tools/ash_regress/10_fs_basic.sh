#!/bin/busybox ash
# ASH_FS_BASIC — mkdir/echo-redir/cat/rm via VFS path ops.
set -eu
mkdir -p /tmp/ash_fs || exit 1
echo hello > /tmp/ash_fs/a.txt || exit 1
out=$(cat /tmp/ash_fs/a.txt) || exit 1
[ "$out" = hello ] || exit 1
cp /tmp/ash_fs/a.txt /tmp/ash_fs/b.txt || exit 1
mv /tmp/ash_fs/b.txt /tmp/ash_fs/c.txt || exit 1
[ -f /tmp/ash_fs/c.txt ] || exit 1
rm /tmp/ash_fs/a.txt /tmp/ash_fs/c.txt || exit 1
rmdir /tmp/ash_fs || exit 1
echo "ASH_FS_BASIC: PASS"
