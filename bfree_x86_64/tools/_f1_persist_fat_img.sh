#!/usr/bin/env bash
# Create a minimal FAT16-looking persist.img (BPB only) for probe smoke.
# Prefer mkfs.fat when available; else write a hand-crafted BPB at LBA0.
set -eu
OUT="${1:-/tmp/bfree-f1-fat.img}"
rm -f "$OUT"
dd if=/dev/zero of="$OUT" bs=1M count=8 status=none
if command -v mkfs.fat >/dev/null 2>&1; then
  mkfs.fat -F 16 -n BFREEFAT "$OUT"
  echo "FAT16 image (mkfs.fat): $OUT"
  exit 0
fi
if command -v mkfs.vfat >/dev/null 2>&1; then
  mkfs.vfat -F 16 -n BFREEFAT "$OUT"
  echo "FAT16 image (mkfs.vfat): $OUT"
  exit 0
fi

# Hand-crafted FAT16 BPB (8MiB, 512 BPS, 4 SPC, 1 reserved, 2 FATs, 512 root ents).
python3 - "$OUT" <<'PY'
import struct, sys
path = sys.argv[1]
with open(path, "r+b") as f:
    sec = bytearray(512)
    sec[0:3] = b"\xEB\x3C\x90"
    sec[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", sec, 11, 512)   # BPS
    sec[13] = 4                            # SPC
    struct.pack_into("<H", sec, 14, 1)     # reserved
    sec[16] = 2                            # FATs
    struct.pack_into("<H", sec, 17, 512)   # root ents
    total = 8 * 1024 * 1024 // 512
    struct.pack_into("<H", sec, 19, total & 0xFFFF)  # total16
    sec[21] = 0xF8
    struct.pack_into("<H", sec, 22, 32)    # FAT size (sectors)
    struct.pack_into("<H", sec, 24, 32)    # SPT
    struct.pack_into("<H", sec, 26, 2)     # heads
    sec[510] = 0x55
    sec[511] = 0xAA
    f.write(sec)
print("FAT16 image (hand BPB):", path)
PY
