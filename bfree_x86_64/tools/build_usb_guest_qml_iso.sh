#!/usr/bin/env bash
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
MIN_QT_DESKTOP_BYTES=100000
RUN_QEMU=0
if [ "${1:-}" = "run" ]; then
  RUN_QEMU=1
fi

export BFREE_ISO_DESKTOP_SHELL=1
export BFREE_MVP_GUEST_QML=1
export BFREE_MVP_SKIP_HOST_DESKTOP=1
export BFREE_QT_GUEST_BUILD_DIR="$GUEST_QT"
export BFREE_QPA_BUILD="${BFREE_QPA_BUILD:-$ROOT/gui_server/integration_gui/bfree_qpa/build}"

echo "=== USB guest QML ISO (bfree.iso) ==="
echo "guest Qt prefix: $GUEST_QT"
echo ""

BFREE_ROOT="$ROOT" bash <(sed 's/\r$//' "$ROOT/tools/ensure_x86_64_elf_toolchain.sh")
echo "[ok] x86_64-elf-g++: $(command -v x86_64-elf-g++)"

if [ -f "$GUEST_QT/lib/libQt6Core.a" ] && [ -f "$GUEST_QT/lib/libQt6Quick.a" ]; then
  echo "[ok] guest Qt libraries present"
else
  echo "[FAIL] guest Qt incomplete. Need libQt6Core.a + libQt6Quick.a under $GUEST_QT/lib" >&2
  echo "  bash tools/build_bfree_qt6_guest.sh  (hours; build on /root/out)" >&2
  exit 1
fi

echo ""
echo "=== Step 1: link guest desktop.elf (Qt + bfree QPA) ==="
BFREE_QT_GUEST_LINKED=0
if bash "$ROOT/tools/build_guest_desktop_elf.sh"; then
  SZ=$(stat -c%s "$ROOT/userland/desktop_qt/desktop.elf" 2>/dev/null || wc -c <"$ROOT/userland/desktop_qt/desktop.elf")
  if [ "$SZ" -ge "$MIN_QT_DESKTOP_BYTES" ]; then
    BFREE_QT_GUEST_LINKED=1
    export BFREE_QT_GUEST_LINKED=1
    echo "[ok] desktop.elf size=$SZ (Qt guest - will replace stub on ISO)"
  else
    echo "[WARN] desktop.elf only $SZ bytes - still a stub; ISO will not show QML" >&2
  fi
else
  echo "[WARN] guest desktop.elf link failed. ISO will use desktop_stub." >&2
  echo "  See tools/GUEST_QT_RESUME_CHECKPOINT.txt section 4c." >&2
fi

echo ""
echo "=== Step 2: stage QML tree on ISO ==="
bash "$ROOT/tools/stage_mvp_qml.sh"

echo ""
echo "=== Step 3: build bfree.iso ==="
bash "$ROOT/build.sh"

ISO="$ROOT/bfree.iso"
if [ ! -f "$ISO" ]; then
  echo "[FAIL] $ISO not created" >&2
  exit 1
fi

echo ""
echo "=== Done ==="
ls -la "$ISO"
if [ "$BFREE_QT_GUEST_LINKED" = "1" ]; then
  echo ""
  echo "USB: Rufus DD image or: sudo dd if=$ISO of=/dev/sdX bs=4M status=progress conv=fsync"
  echo "PC:  Boot USB, GRUB B-Free OS (GUI), login bfree/bfree"
  echo "     VGA: DesktopShell.qml fullscreen (qrc in desktop.elf)."
else
  echo ""
  echo "ISO boots but session is desktop_stub, NOT DesktopShell.qml."
  echo "Fix guest Qt link: bash tools/build_bfree_qt6_guest.sh"
  echo "Then: bash build_iso_desktop_shell.sh"
fi

if [ "$RUN_QEMU" = "1" ]; then
  echo ""
  echo "=== QEMU smoke ==="
  exec qemu-system-x86_64 -cdrom "$ISO" -m 256M -vga std -serial mon:stdio -no-reboot
fi
