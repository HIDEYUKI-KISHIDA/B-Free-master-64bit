#!/usr/bin/env bash
# F1 scaffold: create persist.img and document QEMU -drive wiring.
# Does NOT claim reboot durability yet — block R/W driver still stub.
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
[f1] QEMU attach example (not wired into phase3 yet):
  -drive file=$IMG,if=ide,format=raw,index=1,media=disk
[f1] Guest: /persist remains RAM vfile until AHCI/ATA PIO lands.
[f1] See tools/sf01_persistent_fs_note.md
EOF
