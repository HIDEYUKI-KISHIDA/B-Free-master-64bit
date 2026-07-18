#!/usr/bin/env bash
# Build native host Qt 6 qtbase (tools + libs) for guest cross-compile (QT_HOST_PATH).
# Run on WSL ext4 (/root/out), not under /mnt/c. ~30–90 minutes with jobs=2.
#
#   export BFREE_QT_SRC=/root/src/qt6
#   export BFREE_QT_BUILD_DIR=/root/out/bfree-qt6-static
#   bash tools/build_host_qt_minimal.sh

if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_SRC="${BFREE_QT_SRC:-/root/src/qt6}"
PREFIX="${BFREE_QT_BUILD_DIR:-/root/out/bfree-qt6-static}"
JOBS="${JOBS:-2}"

need() { command -v "$1" >/dev/null 2>&1 || { echo "[host-qt] missing: $1" >&2; exit 1; }; }
need cmake
need ninja
need g++

host_qt_ok() {
  local p="$1"
  [[ -n "$p" && -d "$p" ]] || return 1
  [[ -x "$p/bin/moc" || -x "$p/libexec/moc" ]] || return 1
  [[ -x "$p/bin/qt-cmake" ]] || return 1
  [[ -r "$p/lib/cmake/Qt6/Qt6Config.cmake" ]] || return 1
}

host_qt_ready() {
  local p="$1"
  host_qt_ok "$p" || return 1
  [[ -r "$p/lib/cmake/Qt6ShaderToolsTools/Qt6ShaderToolsToolsConfig.cmake" ]] || return 1
  [[ -r "$p/lib/cmake/Qt6QmlTools/Qt6QmlToolsConfig.cmake" ]] || return 1
}

if host_qt_ready "$PREFIX"; then
  echo "[host-qt] OK: $PREFIX (qtbase + ShaderToolsTools + QmlTools)"
  exit 0
fi

if host_qt_ok "$PREFIX"; then
  echo "[host-qt] qtbase OK — building missing host modules for guest cross-compile"
fi

if [[ ! -f "$QT_SRC/qtbase/CMakeLists.txt" ]]; then
  echo "[host-qt] Qt sources missing: $QT_SRC/qtbase" >&2
  echo "  export BFREE_QT_SRC=/root/src/qt6 && bash $ROOT/tools/clone_qt6_src.sh" >&2
  exit 1
fi

if ! host_qt_ok "$PREFIX"; then
  echo "=== Build host Qt (native qtbase for QT_HOST_PATH) ==="
  echo "  sources: $QT_SRC"
  echo "  prefix:  $PREFIX"
  echo "  jobs:    $JOBS"

  rm -rf "$PREFIX/build-qtbase-host"
  mkdir -p "$PREFIX/build-qtbase-host"
  cd "$PREFIX/build-qtbase-host"

  cmake -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_qtwebengine=OFF \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TESTS=OFF \
    -DINPUT_opengl=no \
    -DFEATURE_vulkan=OFF \
    -DFEATURE_dbus=OFF \
    -DINPUT_openssl=no \
    -DFEATURE_icu=OFF \
    -DFEATURE_glib=OFF \
    -DFEATURE_xcb=OFF \
    -DFEATURE_xkbcommon=OFF \
    -DFEATURE_xcb_xlib=OFF \
    -DFEATURE_system_zlib=OFF \
    -DINPUT_libpng=qt \
    -DINPUT_freetype=qt \
    -DINPUT_harfbuzz=qt \
    -DINPUT_pcre=qt \
    -DFEATURE_fontconfig=OFF \
    "$QT_SRC/qtbase"

  cmake --build . --parallel "$JOBS"
  cmake --install .

  if ! host_qt_ok "$PREFIX"; then
    echo "[host-qt] install finished but prefix check failed: $PREFIX" >&2
    ls -la "$PREFIX/bin" 2>/dev/null | head -10 || true
    exit 1
  fi

  echo "[host-qt] qtbase ready: $PREFIX"
fi

if [[ -r "$PREFIX/lib/cmake/Qt6ShaderToolsTools/Qt6ShaderToolsToolsConfig.cmake" ]]; then
  echo "[host-qt] ShaderToolsTools already present"
else
  echo ""
  echo "=== Build host qtshadertools (qsb for guest cross-compile) ==="
  bash "$ROOT/tools/patch_qt_shadertools_glslang_cstdint.sh"
  mkdir -p "$PREFIX/build-qtshadertools-host"
  cd "$PREFIX/build-qtshadertools-host"
  if [[ ! -f CMakeCache.txt ]]; then
    "$PREFIX/bin/qt-cmake" "$QT_SRC/qtshadertools" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
  fi
  cmake --build . --parallel "$JOBS"
  cmake --install .
  echo "[host-qt] ShaderToolsTools: $PREFIX/lib/cmake/Qt6ShaderToolsTools"
fi

if [[ -r "$PREFIX/lib/cmake/Qt6QmlTools/Qt6QmlToolsConfig.cmake" ]]; then
  echo "[host-qt] QmlTools already present"
else
  echo ""
  echo "=== Build host qtdeclarative (QmlTools for guest cross-compile) ==="
  mkdir -p "$PREFIX/build-qtdeclarative-host"
  cd "$PREFIX/build-qtdeclarative-host"
  if [[ ! -f CMakeCache.txt ]]; then
    "$PREFIX/bin/qt-cmake" "$QT_SRC/qtdeclarative" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DQT_BUILD_EXAMPLES=OFF \
      -DQT_BUILD_TESTS=OFF \
      -DFEATURE_qml_profiler=OFF \
      -DFEATURE_qmlpreview=OFF
  fi
  cmake --build . --parallel "$JOBS"
  cmake --install .
  echo "[host-qt] QmlTools: $PREFIX/lib/cmake/Qt6QmlTools"
fi

echo "[host-qt] ready: $PREFIX"
echo "  export BFREE_QT_BUILD_DIR=$PREFIX"
