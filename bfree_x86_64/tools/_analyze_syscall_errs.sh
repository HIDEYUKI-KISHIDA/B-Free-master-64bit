#!/bin/bash
set -euo pipefail
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program
make -C bfree_x86_64/kernel sysmain/syscall.o 2>&1 | tee /tmp/syscall_build.log >/dev/null || true
echo "error_count=$(grep -c 'error:' /tmp/syscall_build.log || true)"
echo "=== unique errors ==="
grep 'error:' /tmp/syscall_build.log | sed 's/.*error: //' | sort | uniq -c | sort -rn | head -50
echo "=== first 60 error lines ==="
grep -n 'error:' /tmp/syscall_build.log | head -60
