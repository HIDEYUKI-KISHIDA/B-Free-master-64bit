#!/usr/bin/env bash
# Replace /boot/desktop.elf inside bfree.iso without full grub-mkrescue (~seconds vs minutes).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${BFREE_ISO:-$ROOT/bfree.iso}"
SRC="${BFREE_DESKTOP_ELF:-$ROOT/iso_root/boot/desktop.elf}"
ISO_PATH="/boot/desktop.elf"

if [[ ! -f "$SRC" ]]; then
  echo "[fast-iso] missing $SRC" >&2
  exit 1
fi
if [[ ! -f "$ISO" ]]; then
  echo "[fast-iso] no $ISO — falling back to grub-mkrescue"
  grub-mkrescue -o "$ISO" "$ROOT/iso_root" -- -volid BFREE >/dev/null 2>&1
  exit 0
fi
if ! command -v xorriso >/dev/null 2>&1; then
  echo "[fast-iso] xorriso missing — grub-mkrescue fallback"
  grub-mkrescue -o "$ISO" "$ROOT/iso_root" -- -volid BFREE >/dev/null 2>&1
  exit 0
fi

TMP="${ISO}.fasttmp.$$"
cp -f "$ISO" "$TMP"
if xorriso -indev "$TMP" -outdev "$TMP" \
    -boot_image any replay \
    -update "$SRC" "$ISO_PATH" \
    -commit >/dev/null 2>&1; then
  mv -f "$TMP" "$ISO"
  echo "[fast-iso] updated $ISO_PATH ($(stat -c%s "$SRC") bytes, boot replay)"
else
  rm -f "$TMP"
  echo "[fast-iso] xorriso failed — grub-mkrescue fallback"
  grub-mkrescue -o "$ISO" "$ROOT/iso_root" -- -volid BFREE >/dev/null 2>&1
fi
