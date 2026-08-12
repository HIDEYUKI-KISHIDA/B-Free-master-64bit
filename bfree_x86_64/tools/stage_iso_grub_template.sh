#!/usr/bin/env bash
# Copy committed grub.cfg.template into iso_root (iso_root/ is gitignored).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/boot/grub/grub.cfg.template"
DST="$ROOT/iso_root/boot/grub/grub.cfg"
mkdir -p "$(dirname "$DST")"
cp -f "$SRC" "$DST"
echo "[stage_iso_grub] $DST"
