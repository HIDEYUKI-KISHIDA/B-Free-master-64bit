#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ELF="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/desktop.elf"
ADDR="${1:-0x31E752F}"
echo "=== addr2line $ADDR ==="
x86_64-elf-addr2line -f -C -i -e "$ELF" "$ADDR" || true
echo "=== nm before $ADDR ==="
x86_64-elf-nm -n "$ELF" | python3 -c "
import sys
target = int('${ADDR}', 16)
sym = None
for line in sys.stdin:
    parts = line.split()
    if len(parts) >= 3 and parts[0].isdigit() or (len(parts)>=3 and all(c in '0123456789abcdef' for c in parts[0])):
        try:
            a = int(parts[0], 16)
            if a <= target:
                sym = line.rstrip()
        except ValueError:
            pass
print(sym or '(none)')
"
echo "=== disasm window ==="
x86_64-elf-objdump -d "$ELF" | grep -E '31e74|31e75|31e76' | head -20
