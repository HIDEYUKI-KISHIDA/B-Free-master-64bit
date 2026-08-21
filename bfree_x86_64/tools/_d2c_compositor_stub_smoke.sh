#!/usr/bin/env bash
# D2c smoke: 1024×768 Wayland client SHM covers stub chrome.
# Requires: patched kernel (vfile 48KiB × 72 slots), compositor stub ISO,
#           qt_wl_hello.elf when guest Qt prefix exists (else C p8test only).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
LOG="${BFREE_D2C_LOG:-/tmp/bfree_d2c_stub.log}"
ISO="${BFREE_STUB_ISO:-$ROOT/bfree-compositor-stub.iso}"
QEMU_SECS="${BFREE_D2C_QEMU_SECS:-90}"
KERNEL="${BFREE_STUB_KERNEL:-$ROOT/kernel/kernel.elf}"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1" >&2; exit 1; }; }
need xorriso
need qemu-system-x86_64

if [[ -x "$ROOT/tools/verify_d2c_build.sh" ]]; then
  bash "$ROOT/tools/verify_d2c_build.sh" || exit 1
fi

if [[ ! -s "$KERNEL" ]]; then
  echo "missing kernel: $KERNEL (make -C kernel)" >&2
  exit 1
fi
if ! strings "$KERNEL" | grep -qF '[VFORK] eg'; then
  echo "FAIL: $KERNEL lacks [VFORK] eg marker — rebuild with:" >&2
  echo "  make -C kernel clean && make -C kernel RELEASE=1" >&2
  exit 1
fi
if ! strings "$KERNEL" | grep -qF 'build=d2c-vfork-3'; then
  echo "FAIL: $KERNEL lacks build=d2c-vfork-3 — git pull and rebuild kernel" >&2
  exit 1
fi

if [[ ! -s "$ROOT/bfree-desk.iso" && ! -s "$ROOT/bfree.iso" ]]; then
  bash "$ROOT/tools/make_desk_iso.sh"
fi

BFREE_D2B_QML=0 bash "$ROOT/tools/build_qt_wl_hello.sh" || true
bash "$ROOT/tools/build_compositor_stub_iso.sh"

echo "[d2c] inject kernel $KERNEL into $ISO"
cp -f "$ISO" "${ISO}.pre-kernel"
xorriso -indev "$ISO" -outdev "$ISO" \
  -boot_image any replay \
  -map "$KERNEL" /boot/kernel.elf \
  -commit >/dev/null

ISO_KERNEL="$(mktemp)"
trap 'rm -f "$ISO_KERNEL"' EXIT
xorriso -osirrox on -indev "$ISO" -extract /boot/kernel.elf "$ISO_KERNEL" >/dev/null 2>&1
if ! cmp -s "$KERNEL" "$ISO_KERNEL"; then
  echo "FAIL: ISO /boot/kernel.elf differs from $KERNEL (xorriso inject failed?)" >&2
  exit 1
fi
echo "[d2c] ISO kernel OK ($(wc -c < "$KERNEL" | tr -d ' ') bytes)"

rm -f "$LOG"
echo "[d2c] qemu $ISO serial=$LOG (${QEMU_SECS}s)"
timeout "$QEMU_SECS" qemu-system-x86_64 \
  -cdrom "$ISO" -m 1024 -vga std -serial "file:$LOG" -display none \
  2>/dev/null || true

sleep 2
echo "[d2c] serial grep:"
grep -aE 'D2c fullscreen|D2 fill desk|exit_group|vfork parent|VFORK] eg|VFORK] exit_from_fork|VFORK] parent resume|wl shm put=|wl shm get=' "$LOG" | head -40 || true

fail=0
grep -aqF '[KERNEL] build=d2c-vfork-3' "$LOG" || {
  echo "FAIL: serial lacks [KERNEL] build=d2c-vfork-3 (QEMU booted stale kernel?)"
  fail=1
}
grep -aq 'D2c fullscreen' "$LOG" || { echo "MISS: D2c fullscreen"; fail=1; }
if grep -aq 'exit_group' "$LOG"; then
  grep -aqF '[VFORK] eg fa=' "$LOG" || {
    echo "FAIL: exit_group without kernel [VFORK] eg (stale kernel.elf on ISO?)"
    fail=1
  }
fi
grep -aq 'vfork parent' "$LOG" || { echo "MISS: vfork parent"; fail=1; }
if grep -aq 'wl shm put wr=0xffffffffffffffe4' "$LOG"; then
  echo "FAIL: wl shm put EINVAL (kernel vfile < 48KiB?)"
  fail=1
fi
if grep -aq 'wl shm put=0xffffffffffffffe4' "$LOG"; then
  echo "FAIL: wl shm put returned EINVAL aggregate"
  fail=1
fi
if grep -aqE 'wl shm put open=0xffffffffffffffe8|wl shm put=0xffffffffffffffe8' "$LOG"; then
  echo "FAIL: wl shm put EMFILE (vfile slots exhausted? need 72 slots, no /tmp/wlm)"
  fail=1
fi
put_ok="$(grep -ao 'wl shm put=0x[0-9a-f]*' "$LOG" | tail -1 || true)"
if [[ -n "$put_ok" ]]; then
  echo "OK: $put_ok"
fi
grep -aq 'wl shm get=' "$LOG" || { echo "MISS: wl shm get"; fail=1; }
get_ok="$(grep -ao 'wl shm get=0x[0-9a-f]*' "$LOG" | tail -1 || true)"
if [[ -n "$get_ok" && "$get_ok" == *"0000000000000000"* ]]; then
  echo "WARN: parent read 0 shm bytes"
  fail=1
fi
if [[ -f "$ROOT/userland/compositor_stub/qt_wl_hello.elf" ]]; then
  sz="$(wc -c < "$ROOT/userland/compositor_stub/qt_wl_hello.elf")"
  if [[ "$sz" -gt 1000000 ]]; then
    grep -aqF '[qt] build=d2c-vfork-3' "$LOG" || {
      echo "MISS: [qt] build=d2c-vfork-3 (rebuild: BFREE_D2B_QML=0 bash tools/build_qt_wl_hello.sh)"
      fail=1
    }
    grep -aq 'D2 fill desk' "$LOG" || { echo "MISS: D2 fill desk (Qt hello)"; fail=1; }
    grep -aq 'exit_group' "$LOG" || { echo "MISS: exit_group (Qt hello)"; fail=1; }
  fi
fi

if [[ "$fail" != 0 ]]; then
  echo "[d2c] FAIL — tail serial:"
  tail -25 "$LOG"
  exit 1
fi
echo "[d2c] PASS (serial markers OK)"
echo "D2C_LOG=$LOG"
