#!/usr/bin/env python3
from pathlib import Path
import sys

obj = Path(sys.argv[1] if len(sys.argv) > 1 else "qsimd.cpp.o")
orig_size = obj.stat().st_size
data = obj.read_bytes()
# Longest patterns first so 48 0f c7 .. is not split by shorter 0f c7 ..
replacements = [
    (b"\x48\x0f\xc7\xf2", b"\x48\x31\xd2\x90"),  # rdrand %rdx
    (b"\x48\x0f\xc7\xfa", b"\x48\x31\xd2\x90"),  # rdseed %rdx
    (b"\x0f\xc7\xf2", b"\x31\xd2\x90"),          # rdrand %edx
    (b"\x0f\xc7\xfa", b"\x31\xd2\x90"),          # rdseed %edx
]
patched = bytearray(data)
count = 0
for old, new in replacements:
    if len(old) != len(new):
        raise SystemExit(f"length mismatch {old!r} -> {new!r}")
    i = 0
    while True:
        j = patched.find(old, i)
        if j < 0:
            break
        patched[j : j + len(old)] = new
        count += 1
        i = j + len(new)
if count == 0:
    sys.exit("[patch-rdrnd] ERROR: no RDRAND/RDSEED opcodes in qsimd.cpp.o")
if len(patched) != orig_size:
    sys.exit(f"[patch-rdrnd] ERROR: size changed {orig_size} -> {len(patched)}")
obj.write_bytes(patched)
print(f"[patch-rdrnd] replaced {count} opcode(s) in {obj} (size {orig_size})")
