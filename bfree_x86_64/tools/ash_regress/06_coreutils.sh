#!/bin/busybox ash
# ASH_COREUTILS — expanded BusyBox applets via PATH (ls, mkdir, cp, grep, …).
set -eu

WORKDIR="${TMPDIR:-/tmp}/ash_coreutils_$$"
trap 'rm -rf "${WORKDIR}"' EXIT INT TERM
mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

mkdir sub
echo hello > sub/a.txt
echo world > sub/b.txt

[ "$(ls sub | wc -l)" -eq 2 ] || { echo "ASH_COREUTILS: FAIL (ls)"; exit 1; }

cp sub/a.txt sub/c.txt
grep -q hello sub/c.txt || { echo "ASH_COREUTILS: FAIL (cp/grep)"; exit 1; }

mv sub/b.txt sub/d.txt
[ ! -f sub/b.txt ] && [ -f sub/d.txt ] || { echo "ASH_COREUTILS: FAIL (mv)"; exit 1; }

rm sub/c.txt
[ ! -f sub/c.txt ] || { echo "ASH_COREUTILS: FAIL (rm)"; exit 1; }

find sub -name '*.txt' | grep -q d.txt || { echo "ASH_COREUTILS: FAIL (find)"; exit 1; }

echo "ASH_COREUTILS: PASS"
