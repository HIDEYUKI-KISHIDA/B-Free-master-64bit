#!/bin/bash
set -euo pipefail
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program
make -C bfree_x86_64/kernel sysmain/syscall.o 2>&1 | tee /tmp/syscall_build2.log
echo EXIT:${PIPESTATUS[0]}
echo "error_count=$(grep -c 'error:' /tmp/syscall_build2.log || true)"
echo "warning_as_error=$(grep -c 'warnings being treated as errors' /tmp/syscall_build2.log || true)"
echo "=== last 30 lines ==="
tail -n 30 /tmp/syscall_build2.log
echo "=== key markers ==="
F=bfree_x86_64/kernel/sysmain/syscall.c
for m in as_copy inet ptmx exec_transfer bfree_guest_as_copy_switch_heap_to_child BFREE_SYSRET_EXEC_TRANSFER bfree_inet_from_fd g_unix_socks; do
  echo -n "$m: "
  grep -c "$m" "$F" || true
done
