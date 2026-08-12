#!/usr/bin/env bash
# Build native Qt6WaylandScannerTools into host Qt prefix (guest qtwayland cross-build needs this).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST_QT="${BFREE_QT_BUILD_DIR:-${HOME}/out/bfree-qt6-static}"
GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-${HOME}/out/bfree-qt6-guest-static}"
QT_SRC="${BFREE_QT_SRC:-${HOME}/src/qt6}"
QT_TAG="${BFREE_QT_VERSION:-6.8.0}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

host_wayland_tools_ok() {
  [[ -r "$1/lib/cmake/Qt6WaylandScannerTools/Qt6WaylandScannerToolsConfig.cmake" ]] || return 1
  [[ -x "$1/libexec/qtwaylandscanner" || -x "$1/bin/qtwaylandscanner" ]] || return 1
}

if host_wayland_tools_ok "$HOST_QT"; then
  echo "[host-qtwayland] OK: $HOST_QT/lib/cmake/Qt6WaylandScannerTools"
  exit 0
fi

if [[ ! -x "$HOST_QT/bin/qt-cmake" ]]; then
  echo "[host-qtwayland] host Qt missing: $HOST_QT/bin/qt-cmake" >&2
  echo "  export BFREE_QT_BUILD_DIR=$HOST_QT" >&2
  echo "  bash $ROOT/tools/build_host_qt_minimal.sh" >&2
  exit 1
fi

if ! command -v wayland-scanner >/dev/null 2>&1; then
  echo "[host-qtwayland] host wayland-scanner missing:" >&2
  echo "  sudo apt install wayland-protocols libwayland-dev" >&2
  exit 1
fi

if [[ ! -f "$QT_SRC/qtwayland/CMakeLists.txt" ]]; then
  BFREE_QT_SRC="$QT_SRC" BFREE_QT_VERSION="$QT_TAG" \
    bash "$ROOT/tools/build_guest_qtwayland.sh" --fetch-only
fi

BD="$HOST_QT/build-qtwayland-host"
echo "[host-qtwayland] native configure in $BD (prefix=$HOST_QT)"
echo "[host-qtwayland] scanner-only: host qtbase has no Gui wayland feature (build_host_qt_minimal)"
rm -rf "$BD"
mkdir -p "$BD"

# Native (host) build — uses system libwayland from apt, not cross libwayland.
# Disable wayland client/compositor modules: they require QT_FEATURE_wayland in host Qt Gui,
# which minimal host qtbase intentionally omits. qtwaylandscanner only needs QtCore + wayland-scanner.
"$HOST_QT/bin/qt-cmake" -G Ninja "$QT_SRC/qtwayland" \
  -DCMAKE_INSTALL_PREFIX="$HOST_QT" \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -DFEATURE_wayland_client=OFF \
  -DFEATURE_wayland_server=OFF \
  -B "$BD" 2>&1 | tee "$BD/configure.log"

if grep -q "Qt Gui has been built without 'wayland' feature" "$BD/configure.log"; then
  echo "[host-qtwayland] ERROR: configure still enabled wayland client (need FEATURE_wayland_client=OFF)" >&2
  exit 1
fi

if grep -q 'QtWayland is missing required dependencies' "$BD/configure.log"; then
  echo "[host-qtwayland] ERROR: native qtwayland configure skipped (need apt libwayland-dev)" >&2
  exit 1
fi

if grep -q 'Configuring incomplete, errors occurred!' "$BD/configure.log"; then
  echo "[host-qtwayland] ERROR: configure failed — see $BD/configure.log" >&2
  exit 1
fi

echo "[host-qtwayland] building qtwaylandscanner (+ install Tools package) ..."
cmake --build "$BD" --target qtwaylandscanner --parallel "$JOBS"
cmake --install "$BD" --component Devel 2>/dev/null || cmake --install "$BD"

if ! host_wayland_tools_ok "$HOST_QT"; then
  echo "[host-qtwayland] ERROR: Qt6WaylandScannerTools still missing after install" >&2
  ls -la "$HOST_QT/lib/cmake/Qt6WaylandScannerTools" 2>&1 || true
  ls -la "$HOST_QT/libexec/qtwaylandscanner" "$HOST_QT/bin/qtwaylandscanner" 2>&1 || true
  exit 1
fi

echo "[host-qtwayland] OK: $HOST_QT/lib/cmake/Qt6WaylandScannerTools"
echo "  guest qtwayland will use: QT_HOST_PATH=$HOST_QT (guest prefix=$GUEST_QT)"
