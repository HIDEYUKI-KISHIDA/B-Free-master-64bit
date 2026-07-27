#!/bin/busybox ash
# ASH_LN_READLINK — symlink + readlink via Linux ABI.
set -eu
tmp=${TMPDIR:-/tmp}/ash_ln_$$
ln -s /bin/true "$tmp"
got=$(readlink "$tmp")
[ "$got" = "/bin/true" ] || {
	echo "ASH_LN_READLINK: FAIL (got '$got')"
	exit 1
}
echo "ASH_LN_READLINK: PASS"
