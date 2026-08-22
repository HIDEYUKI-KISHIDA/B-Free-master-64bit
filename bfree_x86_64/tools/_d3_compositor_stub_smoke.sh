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
export BFREE_D3_FULL="${BFREE_D3_FULL:-1}"
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
ISO_DESK="$(mktemp)"
trap 'rm -f "$ISO_KERNEL" "$ISO_DESK"' EXIT
xorriso -osirrox on -indev "$ISO" -extract /boot/kernel.elf "$ISO_KERNEL" >/dev/null 2>&1
if ! cmp -s "$KERNEL" "$ISO_KERNEL"; then
  echo "FAIL: ISO /boot/kernel.elf differs from $KERNEL" >&2
  exit 1
fi
echo "[d3] ISO kernel OK ($(wc -c < "$KERNEL" | tr -d ' ') bytes)"

ISO_DESK="$(mktemp)"
xorriso -osirrox on -indev "$ISO" -extract /boot/desktop.elf "$ISO_DESK" >/dev/null 2>&1
if [[ ! -s "$ISO_DESK" ]]; then
  echo "FAIL: ISO missing /boot/desktop.elf" >&2
  exit 1
fi
if ! cmp -s "$DESK" "$ISO_DESK"; then
  iso_sha="$(sha256sum "$ISO_DESK" | awk '{print $1}')"
  disk_sha="$(sha256sum "$DESK" | awk '{print $1}')"
  echo "FAIL: ISO /boot/desktop.elf differs from $DESK" >&2
  echo "  ISO   sha256=$iso_sha ($(wc -c < "$ISO_DESK") bytes)" >&2
  echo "  disk  sha256=$disk_sha ($(wc -c < "$DESK") bytes)" >&2
  echo "  rm -f $ISO && BFREE_D3=1 bash tools/build_compositor_stub_iso.sh" >&2
  exit 1
fi
echo "[d3] ISO desktop.elf OK (matches disk sha256)"

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
    if bash "$ROOT/tools/check_desktop_holder_embedded.sh" "$DESK" >/dev/null 2>&1; then
      if python3 "$ROOT/tools/check_desktop_phdrs_embedded.py" "$DESK" >/dev/null 2>&1; then
        if grep -aq '\[D\]' "$LOG" || grep -aq '\[P\]' "$LOG"; then
          if grep -aq 'qresource ensure list' "$LOG" && ! grep -aqF '[desktop_qt] main entry' "$LOG"; then
            echo "FAIL: guest hang in Qt qresource init (resourceGlobalData BSS @ 0x62c6160):"
            echo "  make -C kernel clean && make -C kernel RELEASE=1   # 16 KiB tail BSS scrub"
            echo "  sha256sum kernel/kernel.elf   # expect tools/kernel.d3.good.sha256"
          else
            echo "FAIL: guest PANIC in Qt init_array after musl TLS ok (static plugin ctor?):"
            echo "  make -C kernel clean && make -C kernel RELEASE=1   # needs [TLS] scrub tail bss"
            echo "  sha256sum kernel/kernel.elf   # expect tools/kernel.d3.good.sha256"
          fi
        elif grep -aqF '[TLS] exec early fsbase=' "$LOG"; then
          echo "FAIL: guest PANIC at musl __init_tls/__copy_tls (tail BSS reuse on vfork exec):"
          echo "  make -C kernel clean && make -C kernel RELEASE=1   # needs [TLS] scrub tail bss"
          echo "  sha256sum kernel/kernel.elf   # expect tools/kernel.d3.good.sha256"
        else
          echo "FAIL: guest PANIC but desktop fingerprint OK — rebuild kernel (TLS bootstrap):"
          echo "  make -C kernel clean && make -C kernel RELEASE=1"
          echo "  sha256sum kernel/kernel.elf   # expect tools/kernel.d3.good.sha256"
        fi
      else
        echo "FAIL: guest PANIC — stale bfree_guest_phdrs (musl __copy_tls on vfork exec):"
        echo "  bash tools/relink_desktop_phdrs_only.sh"
        python3 "$ROOT/tools/check_desktop_phdrs_embedded.py" "$DESK" 2>&1 || true
      fi
    else
      echo "FAIL: guest PANIC (desktop.elf broken — restore or relink):"
      echo "  bash tools/restore_desktop_good_for_d3.sh"
      echo "  bash tools/build_desktop_d3_wayland.sh"
    fi
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

D3_FULL="${BFREE_D3_FULL:-0}"
if [[ "$D3_FULL" == "1" ]]; then
  echo "[d3] FULL tier (BFREE_D3_FULL=1)"
  grep -aqF '[desktop_qt] platform=wayland' "$LOG" || {
    echo "MISS: [desktop_qt] platform=wayland (run build_desktop_d3_wayland.sh)"
    fail=1
  }
  grep -aqF '[desktop_qt] D3 wayland desk session' "$LOG" || {
    echo "MISS: [desktop_qt] D3 wayland desk session"
    fail=1
  }
  grep -aqF '[desktop_qt] D2 fill desk' "$LOG" || {
    echo "MISS: [desktop_qt] D2 fill desk"
    fail=1
  }
  grep -aqF '[desktop_qt] exit_group' "$LOG" || {
    echo "MISS: [desktop_qt] exit_group"
    fail=1
  }
  grep -aq 'vfork parent' "$LOG" || {
    echo "MISS: vfork parent (D3 full requires stub QPA + exit_group)"
    fail=1
  }
  if grep -aqF '[desktop_qt] desk note created' "$LOG"; then
    echo "OK: persist desk note"
  else
    echo "WARN: [desktop_qt] desk note created missing (persist.img not mounted?)"
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
if [[ "${BFREE_D3_FULL:-0}" == "1" ]]; then
  echo "[d3] PASS (D3 full: wayland desktop + vfork parent + D2 fill desk)"
else
  echo "[d3] PASS (desktop vfork exec + kernel wayland env)"
  echo "  D3 full: bash tools/build_desktop_d3_wayland.sh && BFREE_D3=1 BFREE_D3_FULL=1 bash tools/_d3_compositor_stub_smoke.sh"
fi
echo "D3_LOG=$LOG"
