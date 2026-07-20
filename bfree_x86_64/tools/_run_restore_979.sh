#!/usr/bin/env bash
set -eu
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program
export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"

tr -d '\r' < bfree_x86_64/tools/_restore_from_97982f27.py > bfree_x86_64/tools/_restore_from_97982f27.py.lf
mv bfree_x86_64/tools/_restore_from_97982f27.py.lf bfree_x86_64/tools/_restore_from_97982f27.py
python3 bfree_x86_64/tools/_restore_from_97982f27.py

# Fix repair3 cwd issue then run repairs
sed -i 's|Path("tools/_recovered_syscall/_best_stat_fill.c")|Path(__file__).resolve().parent / "_best_stat_fill.c"|' \
  bfree_x86_64/tools/_recovered_syscall/_repair3_syscall.py || true

for s in _repair_syscall.py _repair2_syscall.py _repair3_syscall.py; do
  echo "=== $s ==="
  python3 "bfree_x86_64/tools/_recovered_syscall/$s" || echo WARN
done

make -C bfree_x86_64/kernel sysmain/syscall.o 2>&1 | tee /tmp/r979.log | tail -25
echo "error_count=$(grep -c 'error:' /tmp/r979.log || true)"
grep 'error:' /tmp/r979.log | sed 's/.*error: //' | sort | uniq -c | sort -rn | head -20 || true

for s in g_guest_fork_was_as_copy g_inet_socks /dev/ptmx g_bfree_exec_transfer_rip; do
  printf "%5d  %s\n" "$(grep -cF -- "$s" bfree_x86_64/kernel/sysmain/syscall.c || true)" "$s"
done
wc -c bfree_x86_64/kernel/sysmain/syscall.c
