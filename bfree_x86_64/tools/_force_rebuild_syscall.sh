#!/bin/bash
set -euo pipefail
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program
rm -f bfree_x86_64/kernel/sysmain/syscall.o
make -C bfree_x86_64/kernel sysmain/syscall.o 2>&1 | tee /tmp/syscall_force.log
echo EXIT:${PIPESTATUS[0]}
echo "error_count=$(grep -c 'error:' /tmp/syscall_force.log || true)"
echo "warning_as_error=$(grep -c 'warnings being treated as errors' /tmp/syscall_force.log || true)"
# show if any error lines
grep 'error:' /tmp/syscall_force.log | head -20 || true
ls -la bfree_x86_64/kernel/sysmain/syscall.o
F=bfree_x86_64/kernel/sysmain/syscall.c
echo "=== markers ==="
for m in as_copy inet ptmx PTMX pty exec_transfer; do
  c=$(grep -c "$m" "$F" || true)
  echo "$m: $c"
done
grep -nE 'ptmx|PTMX|/dev/pts|pty_master|PTY_' "$F" | head -20 || true
