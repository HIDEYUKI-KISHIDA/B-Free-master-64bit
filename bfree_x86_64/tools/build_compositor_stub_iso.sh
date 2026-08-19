#!/usr/bin/env bash
# Clone daily bfree.iso into bfree-compositor-stub.iso.
# Does NOT overwrite bfree.iso. Does NOT rebuild kernel (g1-desk binary stays).
# PID1 is init_tramp.elf registered as init.elf; compositor.elf is a module.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SRC_ISO="${BFREE_ISO:-$ROOT/bfree.iso}"
OUT_ISO="${BFREE_STUB_ISO:-$ROOT/bfree-compositor-stub.iso}"
REWRITE="$ROOT/tools/rewrite_grub_compositor_stub.py"

if [[ ! -f "$SRC_ISO" && -f "$ROOT/bfree-desk.iso" ]]; then
  SRC_ISO="$ROOT/bfree-desk.iso"
fi

if [[ "$OUT_ISO" -ef "$SRC_ISO" ]]; then
  echo "refuse: output ISO must not be daily bfree.iso" >&2
  false
fi
if [[ ! -f "$SRC_ISO" ]]; then
  echo "missing daily ISO: $SRC_ISO" >&2
  false
fi
if ! command -v xorriso >/dev/null 2>&1; then
  echo "missing xorriso" >&2
  false
fi

if [[ -x "${HOME}/x86_64-elf-toolchain/bin/x86_64-elf-gcc" ]]; then
  export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
fi

make -C "$ROOT/userland/compositor_stub"
COMP="$ROOT/userland/compositor_stub/compositor.elf"
TRAMP="$ROOT/userland/compositor_stub/init_tramp.elf"
CLIENT="$ROOT/userland/compositor_stub/wl_client.elf"
QTCLI="$ROOT/userland/compositor_stub/qt_wl_client.elf"
HELLO_QT="$ROOT/userland/compositor_stub/qt_wl_hello.elf"
QT_KIND="C p8test (not QGuiApplication)"
if [[ "${BFREE_P8TEST_C:-}" == "1" ]]; then
  echo "P8_FORCE=C (BFREE_P8TEST_C=1)"
elif [[ -f "$HELLO_QT" ]]; then
  HELLO_SZ="$(wc -c < "$HELLO_QT")"
  if [[ "$HELLO_SZ" -gt 1000000 ]]; then
    QTCLI="$HELLO_QT"
    QT_KIND="QGuiApplication qt_wl_hello.elf"
  fi
fi
test -s "$COMP"
test -s "$TRAMP"
test -s "$CLIENT"
test -s "$QTCLI"

WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

xorriso -osirrox on -indev "$SRC_ISO" -extract /boot/grub/grub.cfg "$WORK/grub.cfg" >/dev/null 2>&1
python3 "$REWRITE" "$WORK/grub.cfg" "$WORK/grub.new"
grep -q 'module2 /boot/init_tramp.elf init.elf' "$WORK/grub.new"
grep -q 'module2 /boot/compositor.elf compositor.elf' "$WORK/grub.new"
grep -q 'module2 /boot/hello.elf hello.elf' "$WORK/grub.new"
grep -q 'module2 /boot/p8test.elf p8test.elf' "$WORK/grub.new"

cp -f "$SRC_ISO" "$OUT_ISO"
xorriso -indev "$OUT_ISO" -outdev "$OUT_ISO" \
  -boot_image any replay \
  -map "$TRAMP" /boot/init_tramp.elf \
  -map "$COMP" /boot/compositor.elf \
  -map "$CLIENT" /boot/hello.elf \
  -map "$QTCLI" /boot/p8test.elf \
  -update "$WORK/grub.new" /boot/grub/grub.cfg \
  -commit >/dev/null

echo "STUB_ISO=$OUT_ISO"
echo "STUB_ISO_BYTES=$(wc -c < "$OUT_ISO")"
echo "DAILY_ISO_UNTOUCHED=$SRC_ISO"
echo "DAILY_ISO_BYTES=$(wc -c < "$SRC_ISO")"
echo "COMP_BYTES=$(wc -c < "$COMP")"
echo "HELLO_BYTES=$(wc -c < "$CLIENT")"
echo "GRUB_PID1=init_tramp.elf as init.elf"
echo "GRUB_EXEC=compositor.elf"
echo "GRUB_CLIENT=hello.elf"
echo "GRUB_QT_CLIENT=p8test.elf"
echo "P8_KIND=$QT_KIND"
echo "P8_BYTES=$(wc -c < "$QTCLI")"
if grep -aq 'hello hybrid-qpa' "$QTCLI" 2>/dev/null; then
  echo "P8TEST_STAMP=hybrid-qpa"
else
  echo "P8TEST_STAMP=MISSING (stale p8test — apply qt_wl_hello.cpp and rebuild tools/build_qt_wl_hello.sh)"
fi
if grep -aq 'QPA factory keys' "$QTCLI" 2>/dev/null; then
  echo "P8TEST_QPA=factory-override"
else
  echo "P8TEST_QPA=MISSING (stale qbfree_wayland.cpp — rebuild will not skip QFactoryLoader)"
fi
if grep -aq 'window start' "$QTCLI" 2>/dev/null; then
  echo "P8TEST_WINDOW=breadcrumbs"
else
  echo "P8TEST_WINDOW=MISSING (stale qt_wl_hello.cpp — no post-ctor serials)"
fi
echo "TRAMP_BYTES=$(wc -c < "$TRAMP")"
echo "KERNEL_REBUILD=no"
