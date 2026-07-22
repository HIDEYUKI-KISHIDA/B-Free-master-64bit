#!/usr/bin/env bash
# F1: create persist.img for QEMU IDE attach (ATA PIO + BFP1 /persist store).
# Full reboot durability is exercised by tools/_f1_persist_smoke.sh.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${BFREE_PERSIST_IMG:-$ROOT/persist.img}"
SIZE_MB="${BFREE_PERSIST_IMG_MB:-8}"

if [[ ! -f "$IMG" ]]; then
  dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
  echo "[f1] created $IMG (${SIZE_MB}MiB zeroed)"
else
  echo "[f1] exists $IMG ($(wc -c <"$IMG") bytes)"
fi

cat <<EOF
[f1] QEMU attach (matches _f1_persist_smoke.sh):
  -drive file=$IMG,if=ide,index=0,media=disk,format=raw
[f1] Guest /persist is vfile-mirrored to BFP1 records on that disk (ATA PIO).
[f1] Smoke: tools/_f1_persist_smoke.sh
[f1] Note: tools/sf01_persistent_fs_note.md
EOF
