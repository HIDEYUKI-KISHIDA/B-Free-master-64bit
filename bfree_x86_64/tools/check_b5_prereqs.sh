#!/usr/bin/env bash
# B5: check host smoke vs guest ISO Qt prerequisites.
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST_QT="${BFREE_QT_BUILD_DIR:-}"
GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
QPA_LIB="$ROOT/gui_server/integration_gui/bfree_qpa/build/plugins/platforms/libqbfree.a"
ISO_QML="$ROOT/iso_root/bfree/qml/GuestMvpShell.qml"

echo "=== B5 prerequisite check ==="

if [[ -f "$ISO_QML" ]]; then
  echo "[OK] B5a ISO QML staged: $ISO_QML"
else
  echo "[!!] Run: bash $ROOT/tools/stage_mvp_qml.sh"
fi

if [[ -n "$HOST_QT" && -f "$HOST_QT/lib/libQt6Core.a" ]]; then
  echo "[OK] B3 host Qt: $HOST_QT/lib/libQt6Core.a"
else
  echo "[--] B3 host Qt missing: set BFREE_QT_BUILD_DIR=/root/out/bfree-qt6-static"
fi

if [[ -f "$QPA_LIB" ]]; then
  echo "[OK] bfree QPA: $QPA_LIB"
else
  echo "[!!] Build QPA with CONFIG+=guestinput in bfree_qpa/build"
fi

if [[ -f "$ROOT/userland/desktop_qt/desktop" ]]; then
  echo "[OK] Host smoke binary: userland/desktop_qt/desktop"
else
  echo "[--] Host smoke: make -f Makefile.bfree qmake-guest"
fi

if [[ -f "$GUEST_QT/lib/libQt6Core.a" ]]; then
  echo "[OK] B3 guest Qt: $GUEST_QT/lib/libQt6Core.a"
  echo "     Next: bash $ROOT/tools/build_guest_desktop_elf.sh"
else
  echo "[--] B3 guest Qt NOT built: $GUEST_QT/lib/libQt6Core.a"
  echo "     Run: export BFREE_QT_GUEST_BUILD_DIR=$GUEST_QT"
  echo "          bash $ROOT/tools/build_bfree_qt6_guest.sh"
fi

GUEST_ELF="$ROOT/userland/desktop_qt/desktop.elf"
if [[ -f "$GUEST_ELF" ]]; then
  sz=$(stat -c%s "$GUEST_ELF" 2>/dev/null || wc -c <"$GUEST_ELF")
  if [[ "$sz" -gt 65536 ]]; then
    echo "[OK] guest desktop.elf Qt-sized ($sz bytes)"
  else
    echo "[--] guest desktop.elf is small ($sz bytes), stub not Qt"
  fi
else
  echo "[--] No $GUEST_ELF"
fi

echo "=== done ==="
