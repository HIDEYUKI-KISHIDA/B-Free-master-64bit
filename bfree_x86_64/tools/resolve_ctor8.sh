#!/usr/bin/env bash
set -euo pipefail
export PATH="/root/x86_64-elf-toolchain/bin:${PATH:-}"
ELF="${1:-$(dirname "$0")/../userland/desktop_qt/desktop.elf}"
ADDR="${2:-0x2925870}"

echo "ELF=$ELF"
echo "=== addr2line $ADDR ==="
x86_64-elf-addr2line -f -e "$ELF" "$ADDR" || true
echo "=== nm (nearest before $ADDR) ==="
x86_64-elf-nm -n --defined-only "$ELF" 2>/dev/null | awk -v t="$(printf '%d' "$ADDR")" '
$1 ~ /^[0-9a-fA-F]+$/ {
  a = strtonum("0x" $1)
  if (a <= t) { best = $0; ba = a }
}
END { if (best != "") print best; else print "(no symbol)" }'
echo "=== .init_array entries (VA, first 24) ==="
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$ELF" <<'PY' 2>/dev/null || true
import struct, subprocess, sys
elf = sys.argv[1]
out = subprocess.check_output(["readelf", "-W", "-S", elf], text=True, errors="replace")
init_off = init_va = init_size = None
for line in out.splitlines():
    if ".init_array" in line and "PROGBITS" in line:
        p = line.split()
        init_va = int(p[4], 16)
        init_off = int(p[5], 16)
        init_size = int(p[6], 16)
        break
if init_va is None:
    print("no .init_array section")
    sys.exit(0)
data = open(elf, "rb").read()
chunk = data[init_off:init_off + init_size]
n = len(chunk) // 8
print(f".init_array VA=0x{init_va:x} size={init_size} count={n}")
for i in range(min(n, 24)):
    ptr = struct.unpack_from("<Q", chunk, i * 8)[0]
    tag = " (ctor[8])" if i == 8 else ""
    print(f"  [{i:2d}] 0x{ptr:016x}{tag}")
PY
