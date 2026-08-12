#!/usr/bin/env bash
# Add QtWayland to an existing guest Qt static prefix (after qtbase + qtdeclarative).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
QT_TAG="${BFREE_QT_VERSION:-6.8.0}"

if [[ ! -f "$GUEST_QT/lib/libQt6Core.a" ]]; then
  echo "[qtwayland-guest] guest qtbase missing under $GUEST_QT" >&2
  echo "  bash $ROOT/tools/build_bfree_qt6_guest.sh" >&2
  exit 1
fi

fetch_qtwayland() {
  echo "[qtwayland-guest] qtwayland sources missing — fetching into $QT_SRC/qtwayland"
  if [[ -x "$QT_SRC/init-repository" ]]; then
    (cd "$QT_SRC" && ./init-repository --module-subset=qtwayland)
  elif [[ -f "$QT_SRC/init-repository" ]]; then
    (cd "$QT_SRC" && perl init-repository --module-subset=qtwayland)
  elif [[ ! -d "$QT_SRC/qtwayland/.git" ]]; then
    git clone --branch "v${QT_TAG}" --depth 1 https://code.qt.io/qt/qtwayland.git \
      "$QT_SRC/qtwayland" 2>/dev/null \
      || git clone --branch "$QT_TAG" --depth 1 https://code.qt.io/qt/qtwayland.git \
      "$QT_SRC/qtwayland"
  fi
  if [[ ! -f "$QT_SRC/qtwayland/CMakeLists.txt" ]]; then
    echo "[qtwayland-guest] fetch failed — still no CMakeLists.txt under $QT_SRC/qtwayland" >&2
    echo "  Try manually:" >&2
    echo "    cd $QT_SRC && ./init-repository --module-subset=qtwayland" >&2
    echo "  Or: git clone https://code.qt.io/qt/qtwayland.git $QT_SRC/qtwayland" >&2
    exit 1
  fi
}

if [[ ! -f "$QT_SRC/qtwayland/CMakeLists.txt" ]]; then
  fetch_qtwayland
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
