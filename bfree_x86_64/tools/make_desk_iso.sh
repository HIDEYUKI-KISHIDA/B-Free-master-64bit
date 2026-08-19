#!/usr/bin/env bash
# Desk ISO from local goldens or GitHub Release. Does NOT write daily bfree.iso.
# Does NOT rebuild kernel or desktop.elf.
# Output: bfree-desk.iso (same Qt desk as daily, new filename).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

OUT="${BFREE_DESK_ISO:-$ROOT/bfree-desk.iso}"
DAILY="$ROOT/bfree.iso"
MIN_DESKTOP="${BFREE_MIN_DESKTOP_BYTES:-10000000}"
REL_TAG="${BFREE_DESK_GOLDENS_TAG:-desk-goldens-1}"
REL_BASE="${BFREE_DESK_GOLDENS_BASE:-https://github.com/HIDEYUKI-KISHIDA/B-Free-master-64bit/releases/download/${REL_TAG}}"

if [[ -e "$DAILY" ]] && [[ "$OUT" -ef "$DAILY" ]]; then
  echo "refuse: will not overwrite daily bfree.iso" >&2
  false
fi
if [[ "$(basename "$OUT")" == "bfree.iso" ]]; then
  echo "refuse: output must not be named bfree.iso" >&2
  false
fi

bytes() { wc -c < "$1"; }

if [[ -s "$DAILY" ]]; then
  cp -f "$DAILY" "$OUT"
  echo "DESK_ISO=$OUT"
  echo "DESK_ISO_BYTES=$(bytes "$OUT")"
  echo "DESK_SOURCE=clone-daily-bfree.iso"
  echo "DAILY_ISO_UNTOUCHED=$DAILY"
  echo "THIS_IS_QT_DESK=1"
  echo "KERNEL_REBUILD=no"
  echo "DESKTOP_RELINK=no"
  exit 0
fi

fetch_golden() {
  local dest="$1" name="$2"
  if [[ -s "$dest" ]]; then
    return 0
  fi
  mkdir -p "$(dirname "$dest")"
  echo "FETCH ${REL_BASE}/${name}"
  curl -L --fail --retry 3 --retry-delay 2 -o "${dest}.part" "${REL_BASE}/${name}"
  mv -f "${dest}.part" "$dest"
}

# Clone has no daily ISO: take local goldens, else download Release assets.
fetch_golden "$ROOT/kernel/kernel.elf.g1-desk" "kernel.elf.g1-desk"
fetch_golden "$ROOT/userland/desktop_qt/desktop.elf" "desktop.elf"
fetch_golden "$ROOT/iso_skel/desk/boot/init.elf" "init.elf"
fetch_golden "$ROOT/iso_skel/desk/boot/busybox.elf" "busybox.elf"

KERNEL=""
for p in \
  "$ROOT/kernel/kernel.elf.g1-desk" \
  "$ROOT/iso_root/boot/kernel.elf"
do
  if [[ -s "$p" ]]; then
    KERNEL="$p"
    break
  fi
done

DESK=""
for p in \
  "$ROOT/userland/desktop_qt/desktop.elf" \
  "$ROOT/iso_root/boot/desktop.elf"
do
  if [[ -s "$p" ]]; then
    DESK="$p"
    break
  fi
done

INIT=""
for p in \
  "$ROOT/iso_skel/desk/boot/init.elf" \
  "$ROOT/iso_root/boot/init.elf" \
  "$ROOT/userland/init/init.elf"
do
  if [[ -s "$p" ]]; then
    INIT="$p"
    break
  fi
done

BUSY=""
for p in \
  "$ROOT/iso_skel/desk/boot/busybox.elf" \
  "$ROOT/iso_root/boot/busybox.elf" \
  "$ROOT/userland/busybox_guest/busybox.elf"
do
  if [[ -s "$p" ]]; then
    BUSY="$p"
    break
  fi
done

GRUB=""
for p in \
  "$ROOT/iso_skel/desk/boot/grub/grub.cfg" \
  "$ROOT/iso_root/boot/grub/grub.cfg"
do
  if [[ -s "$p" ]]; then
    GRUB="$p"
    break
  fi
done

miss=0
if [[ -z "$KERNEL" ]]; then echo "MISSING kernel.elf.g1-desk" >&2; miss=1; fi
if [[ -z "$DESK" ]]; then echo "MISSING desktop.elf (Qt guest)" >&2; miss=1; fi
if [[ -n "$DESK" ]]; then
  ds=$(bytes "$DESK")
  if [[ "$ds" -lt "$MIN_DESKTOP" ]]; then
    echo "refuse: desktop.elf is $ds bytes (need >= $MIN_DESKTOP). That is a stub." >&2
    miss=1
  fi
fi
if [[ -z "$INIT" ]]; then echo "MISSING init.elf" >&2; miss=1; fi
if [[ -z "$GRUB" ]]; then echo "MISSING grub.cfg" >&2; miss=1; fi
if [[ "$miss" != 0 ]]; then
  echo "NEED_DESK_GOLDENS_RELEASE=1" >&2
  echo "upload tag ${REL_TAG}: kernel.elf.g1-desk desktop.elf init.elf busybox.elf" >&2
  false
fi

WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/boot/grub"
cp -f "$GRUB" "$WORK/boot/grub/grub.cfg"
cp -f "$KERNEL" "$WORK/boot/kernel.elf"
cp -f "$INIT" "$WORK/boot/init.elf"
cp -f "$DESK" "$WORK/boot/desktop.elf"
if [[ -n "$BUSY" ]]; then
  cp -f "$BUSY" "$WORK/boot/busybox.elf"
fi
grub-mkrescue -o "$OUT" "$WORK"

echo "DESK_ISO=$OUT"
echo "DESK_ISO_BYTES=$(bytes "$OUT")"
echo "DESK_SOURCE=assemble-goldens"
echo "KERNEL=$KERNEL"
echo "DESKTOP=$DESK"
echo "DAILY_ISO_UNTOUCHED=$DAILY"
echo "THIS_IS_QT_DESK=1"
echo "KERNEL_REBUILD=no"
echo "DESKTOP_RELINK=no"
