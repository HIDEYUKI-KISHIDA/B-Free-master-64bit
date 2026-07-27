#!/bin/busybox ash
# ASH_ID — id -u via getuid (numeric; no /etc/passwd required).
set -eu
uid=$(id -u)
case "$uid" in
	''|*[!0-9]*)
		echo "ASH_ID: FAIL (got uid='$uid')"
		exit 1
		;;
esac
echo "ASH_ID: PASS"
