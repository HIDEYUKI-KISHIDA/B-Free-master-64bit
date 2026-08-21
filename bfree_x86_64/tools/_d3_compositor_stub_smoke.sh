#!/usr/bin/env bash
# D3 smoke: compositor vfork-execs desktop.elf with QT_QPA_PLATFORM=wayland.
# Requires: patched bootable kernel (D2c vfork + 48KiB×72 vfile), stub ISO with
#           desktop.elf >= 10MB, compositor built with BFREE_D3=1.
# Do not map an unpatched from-source kernel onto the stub ISO.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
export BFREE_D3=1
LOG="${BFREE_D3_LOG:-/tmp/bfree_d3_stub.log}"
ISO="${BFREE_STUB_ISO:-$ROOT/bfree-compositor-stub.iso}"
QEMU_SECS="${BFREE_D3_QEMU_SECS:-120}"
KERNEL="${BFREE_STUB_KERNEL:-$ROOT/kernel/kernel.elf}"
DESK="$ROOT/userland/desktop_qt/desktop.elf"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1" >&2; exit 1; }; }
need xorriso
need qemu-system-x86_64

if [[ -x "$ROOT/tools/verify_d3_build.sh" ]]; then
  bash "$ROOT/tools/verify_d3_build.sh" || exit 1
fi

if [[ ! -s "$KERNEL" ]]; then
  echo "missing kernel: $KERNEL (make -C kernel)" >&2
  exit 1
fi
if ! strings "$KERNEL" | grep -qF '[VFORK] immute ok'; then
  echo "FAIL: $KERNEL lacks [VFORK] immute ok — clean-rebuild:" >&2
  echo "  make -C kernel clean && make -C kernel RELEASE=1" >&2
  exit 1
fi
if ! strings "$KERNEL" | grep -qF '[D3] desktop wayland exec'; then
  echo "FAIL: $KERNEL lacks [D3] desktop wayland exec — git pull 後に再ビルド:" >&2
  echo "  make -C kernel clean && make -C kernel RELEASE=1" >&2
  exit 1
fi
if [[ ! -s "$DESK" ]] || [[ "$(wc -c < "$DESK")" -lt 10000000 ]]; then
  echo "FAIL: missing or tiny $DESK (need >=10MB guest desktop.elf)" >&2
  exit 1
fi

if [[ ! -s "$ROOT/bfree-desk.iso" && ! -s "$ROOT/bfree.iso" ]]; then
  bash "$ROOT/tools/make_desk_iso.sh"
fi

BFREE_D3=1 bash "$ROOT/tools/build_compositor_stub_iso.sh"

echo "[d3] inject kernel $KERNEL into $ISO"
cp -f "$ISO" "${ISO}.pre-kernel"
xorriso -indev "$ISO" -outdev "$ISO" \
  -boot_image any replay \
  -map "$KERNEL" /boot/kernel.elf \
  -commit >/dev/null

ISO_KERNEL="$(mktemp)"
trap 'rm -f "$ISO_KERNEL"' EXIT
xorriso -osirrox on -indev "$ISO" -extract /boot/kernel.elf "$ISO_KERNEL" >/dev/null 2>&1
if ! cmp -s "$KERNEL" "$ISO_KERNEL"; then
  echo "FAIL: ISO /boot/kernel.elf differs from $KERNEL" >&2
  exit 1
fi
echo "[d3] ISO kernel OK ($(wc -c < "$KERNEL" | tr -d ' ') bytes)"

rm -f "$LOG"
echo "[d3] qemu $ISO serial=$LOG (${QEMU_SECS}s)"
timeout "$QEMU_SECS" qemu-system-x86_64 \
  -cdrom "$ISO" -m 1024 -vga std -serial "file:$LOG" -display none -no-reboot \
  2>/dev/null || true

sleep 2
echo "[d3] serial grep:"
grep -aE '\[D3\]|desktop\.elf|desktop_qt|VFORK]|vfork parent|exit_group|exec transfer' "$LOG" | head -50 || true

fail=0
grep -aqF '[D3] execve desktop.elf' "$LOG" || { echo "MISS: [D3] execve desktop.elf"; fail=1; }
grep -aqF '[D3] desktop wayland exec' "$LOG" || { echo "MISS: [D3] desktop wayland exec"; fail=1; }
grep -aq 'exec transfer desktop.elf' "$LOG" || { echo "MISS: exec transfer desktop.elf"; fail=1; }
grep -aqF '[desktop_qt] main entry' "$LOG" || {
  echo "MISS: [desktop_qt] main entry"
  if grep -aq '\[PANIC\]' "$LOG"; then
    echo "FAIL: guest PANIC (desktop.elf broken — holder VA mismatch after partial relink):"
    echo "  bash tools/restore_desktop_good_for_d3.sh"
    echo "  bash tools/build_desktop_d3_wayland.sh   # Wayland relink with holder converge"
  fi
  fail=1
}
if grep -aq 'vfork parent' "$LOG"; then
  echo "OK: vfork parent resume (D3 full)"
else
  echo "WARN: vfork parent missing — desktop still on bfree QPA until relink:"
  echo "  bash tools/build_desktop_d3_wayland.sh"
  if grep -aqF '[desktop_qt] platform=bfree' "$LOG"; then
    echo "WARN: [desktop_qt] platform=bfree (expected until D3 desktop relink)"
  fi
fi
if grep -aq 'exit_group' "$LOG"; then
  grep -aqF '[VFORK] eg fa=' "$LOG" || {
    echo "FAIL: exit_group without kernel [VFORK] eg (stale kernel?)"
    fail=1
  }
fi

if grep -aqF '[D3] execve rc=' "$LOG"; then
  rc="$(grep -ao '\[D3\] execve rc=0xffffffff[^ ]*' "$LOG" | head -1 || true)"
  if [[ -n "$rc" && "$rc" != *"0000000000000000"* ]]; then
    echo "WARN: desktop execve returned ($rc) — fell back to p8test?"
  fi
fi

if [[ "$fail" != 0 ]]; then
  echo "[d3] FAIL — tail serial:"
  tail -40 "$LOG"
  echo "[d3] desktop fingerprint:"
  bash "$ROOT/tools/check_desktop_holder_embedded.sh" "$DESK" 2>&1 || true
  exit 1
fi
echo "[d3] PASS (desktop vfork exec + kernel wayland env)"
echo "D3_LOG=$LOG"
