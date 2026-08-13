#!/usr/bin/env bash
# Build desktop.elf as a Wayland client (BFREE_GUEST_WAYLAND_CLIENT=1).
# Requires guest Qt with QtWayland module — see tools/build_guest_qtwayland.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export BFREE_GUEST_WAYLAND_CLIENT=1
export BFREE_QT_GUEST_BUILD_DIR="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"

GUEST_QT="$BFREE_QT_GUEST_BUILD_DIR"
for lib in libQt6WaylandClient.a libQt6WaylandClient.prl; do
  if [[ ! -f "$GUEST_QT/lib/$lib" ]]; then
    echo "[wayland-desktop] missing $GUEST_QT/lib/$lib" >&2
    echo "  Run: bash $ROOT/tools/build_guest_qtwayland.sh" >&2
    exit 1
  fi
done

echo "[wayland-desktop] linking desktop.elf with -platform wayland (no libqbfree.a)"
BFREE_GUEST_WAYLAND_CLIENT=1 bash "$ROOT/tools/build_guest_desktop_elf.sh" "$@" || {
  echo "[wayland-desktop] NOTE: build_guest_desktop_elf.sh still links bfree QPA by default." >&2
  echo "  Pass BFREE_GUEST_WAYLAND_CLIENT=1 to Makefile.bfree guest-elf when qtwayland is wired." >&2
  exit 1
}
