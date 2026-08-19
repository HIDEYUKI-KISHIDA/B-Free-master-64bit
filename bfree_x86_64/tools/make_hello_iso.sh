#!/usr/bin/env bash
# Clone-friendly hello ISO. Does NOT write daily bfree.iso.
# Output: bfree-hello.iso (kernel + embedded user_hello fallback). Not the Qt desk.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

OUT="${BFREE_HELLO_ISO:-$ROOT/bfree-hello.iso}"
SKEL="$ROOT/iso_skel"
DAILY="$ROOT/bfree.iso"

if [[ ! -d "$SKEL/boot/grub" ]]; then
  echo "missing iso_skel/boot/grub" >&2
  false
fi
if [[ -e "$DAILY" ]] && [[ "$OUT" -ef "$DAILY" ]]; then
  echo "refuse: will not overwrite daily bfree.iso" >&2
  false
fi
case "$OUT" in
  *bfree.iso)
    if [[ "$(basename "$OUT")" == "bfree.iso" ]]; then
      echo "refuse: output must not be named bfree.iso" >&2
      false
    fi
    ;;
esac

if [[ -x "${HOME}/x86_64-elf-toolchain/bin/x86_64-elf-gcc" ]]; then
  export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
fi
if [[ -x /root/x86_64-elf-toolchain/bin/x86_64-elf-gcc ]]; then
  export PATH="/root/x86_64-elf-toolchain/bin:${PATH:-}"
fi

make -C "$ROOT/kernel"
test -s "$ROOT/kernel/kernel.elf"

WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

cp -a "$SKEL/." "$WORK/"
mkdir -p "$WORK/boot"
cp -f "$ROOT/kernel/kernel.elf" "$WORK/boot/kernel.elf"
test -s "$WORK/boot/grub/grub.cfg"

grub-mkrescue -o "$OUT" "$WORK"

echo "HELLO_ISO=$OUT"
echo "HELLO_ISO_BYTES=$(wc -c < "$OUT")"
echo "KERNEL_BYTES=$(wc -c < "$ROOT/kernel/kernel.elf")"
echo "DAILY_ISO_UNTOUCHED=$DAILY"
echo "THIS_IS_NOT_QT_DESK=1"
echo "THIS_IS_NOT_G1_DESK=1"
