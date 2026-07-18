#!/usr/bin/env bash
# Build qtshadertools + qtdeclarative after qtbase guest install.
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail

export PATH="${PATH:-/root/x86_64-elf-toolchain/bin:/usr/bin:/bin}"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
QT_SRC="${QT_SRC:-/root/src/qt6}"
JOBS="${JOBS:-2}"

HOST_QT="${QT_HOST_PATH:-/root/out/bfree-qt6-static}"
if [[ ! -r "$HOST_QT/lib/cmake/Qt6ShaderToolsTools/Qt6ShaderToolsToolsConfig.cmake" ]] \
   || [[ ! -r "$HOST_QT/lib/cmake/Qt6QmlTools/Qt6QmlToolsConfig.cmake" ]]; then
  echo "[guest-modules] building missing host Qt modules at $HOST_QT ..."
  ROOT="$(cd "$(dirname "$0")/.." && pwd)"
  BFREE_QT_BUILD_DIR="$HOST_QT" bash "$ROOT/tools/build_host_qt_minimal.sh"
fi

echo "=== qtshadertools ==="
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
bash "$ROOT/tools/patch_qt_shadertools_glslang_cstdint.sh"
mkdir -p "$PREFIX/build-qtshadertools"
cd "$PREFIX/build-qtshadertools"
if [[ ! -f CMakeCache.txt ]]; then
  "$PREFIX/bin/qt-cmake" "$QT_SRC/qtshadertools" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
fi
cmake --build . --parallel "$JOBS"
cmake --install .

echo ""
echo "=== qtdeclarative (Qml + Quick only) ==="
rm -rf "$PREFIX/build-qtdeclarative"
mkdir -p "$PREFIX/build-qtdeclarative"
cd "$PREFIX/build-qtdeclarative"
"$PREFIX/bin/qt-cmake" "$QT_SRC/qtdeclarative" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DQT_BUILD_TOOLS=OFF \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -DFEATURE_qml_profiler=OFF \
  -DFEATURE_qmlpreview=OFF
cmake --build . --target Qml QmlModels QmlMeta QmlCore Quick --parallel "$JOBS"
cmake --install . || true
cp -an "$PREFIX/build-qtdeclarative/lib"/libQt6Qml*.a \
       "$PREFIX/build-qtdeclarative/lib"/libQt6Quick*.a \
       "$PREFIX/lib/" 2>/dev/null || true
# cmake --install can fail on tooling libs (QT_BUILD_TOOLS=OFF); sync headers from build tree.
for mod in QtQml QtQmlMeta QtQmlModels QtQmlWorkerScript QtQmlIntegration QtQmlCore QtQuick; do
  if [[ -d "$PREFIX/build-qtdeclarative/include/$mod" ]]; then
    mkdir -p "$PREFIX/include"
    cp -a "$PREFIX/build-qtdeclarative/include/$mod" "$PREFIX/include/"
  fi
done

echo ""
echo "[ok] guest Qt modules:"
ls -la "$PREFIX/lib/libQt6Core.a" "$PREFIX/lib/libQt6Quick.a" 2>/dev/null || true
