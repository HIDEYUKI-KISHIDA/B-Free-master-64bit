#!/usr/bin/env bash
# Add QtWayland to an existing guest Qt static prefix (after qtbase + qtdeclarative).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"

if [[ ! -f "$GUEST_QT/lib/libQt6Core.a" ]]; then
  echo "[qtwayland-guest] guest qtbase missing under $GUEST_QT" >&2
  echo "  bash $ROOT/tools/build_bfree_qt6_guest.sh" >&2
  exit 1
fi
if [[ ! -d "$QT_SRC/qtwayland" ]]; then
  echo "[qtwayland-guest] qtwayland sources missing: $QT_SRC/qtwayland" >&2
  exit 1
fi

BD="$GUEST_QT/build-qtwayland"
mkdir -p "$BD"
echo "[qtwayland-guest] configure in $BD"
"$GUEST_QT/bin/qt-cmake" "$QT_SRC/qtwayland" \
  -DCMAKE_INSTALL_PREFIX="$GUEST_QT" \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -B "$BD"
cmake --build "$BD" --target WaylandClient QWaylandIntegrationPlugin --parallel "$(nproc 2>/dev/null || echo 4)"
cmake --install "$BD" --component Devel 2>/dev/null || cmake --install "$BD"
echo "[qtwayland-guest] done: $GUEST_QT/lib/libQt6WaylandClient.a"
