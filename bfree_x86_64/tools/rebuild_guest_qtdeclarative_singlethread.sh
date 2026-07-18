#!/usr/bin/env bash
# Rebuild guest Qt6 Qml/Quick with single-threaded QQmlTypeLoader + no QML JIT.
# Run on WSL ext4 (/root/out). Does NOT rebuild qtbase.
#
#   export BFREE_QT_SRC=/root/src/qt6
#   export BFREE_QT_GUEST_BUILD_DIR=/root/out/bfree-qt6-guest-static
#   export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
#   bash tools/rebuild_guest_qtdeclarative_singlethread.sh

set -euo pipefail

if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_SRC="${BFREE_QT_SRC:-/root/src/qt6}"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
JOBS="${JOBS:-2}"
STUB_SRC="$ROOT/gui_server/integration_gui/qtdeclarative/src/qml/qml/ftw/qqmlthread_stub.cpp"
FTW="$QT_SRC/qtdeclarative/src/qml/qml/ftw"
CMAKELISTS="$QT_SRC/qtdeclarative/src/qml/CMakeLists.txt"
GUEST_THREAD="$FTW/qqmlthread_guest_single.cpp"
MARKER="$FTW/.bfree_guest_singlethread_applied"

need() { command -v "$1" >/dev/null 2>&1 || { echo "[FAIL] missing: $1" >&2; exit 1; }; }
need cmake
need ninja

if [[ ! -f "$PREFIX/bin/qt-cmake" ]]; then
  echo "[FAIL] guest Qt prefix missing: $PREFIX/bin/qt-cmake" >&2
  exit 1
fi
if [[ ! -f "$STUB_SRC" ]]; then
  echo "[FAIL] stub source missing: $STUB_SRC" >&2
  exit 1
fi

echo "=== B-Free guest qtdeclarative: single-thread type loader + no JIT ==="
echo "  Qt src:  $QT_SRC"
echo "  prefix:  $PREFIX"
echo "  jobs:    $JOBS"

mkdir -p "$FTW"
if [[ ! -f "$MARKER" ]]; then
  echo "[patch] install QQmlThread single-thread stub"
  if [[ -f "$FTW/qqmlthread.cpp" && ! -f "$FTW/qqmlthread.cpp.bfree_bak" ]]; then
    cp -a "$FTW/qqmlthread.cpp" "$FTW/qqmlthread.cpp.bfree_bak"
  fi
  cp "$ROOT/tools/qqmlthread_guest_single.cpp" "$GUEST_THREAD"
  if grep -q 'qml/ftw/qqmlthread.cpp' "$CMAKELISTS"; then
    sed -i 's|qml/ftw/qqmlthread.cpp|qml/ftw/qqmlthread_guest_single.cpp|' "$CMAKELISTS"
  fi
  touch "$MARKER"
else
  echo "[patch] refreshing QQmlThread stub source"
  cp "$ROOT/tools/qqmlthread_guest_single.cpp" "$GUEST_THREAD"
fi

mkdir -p "$PREFIX/build-qtdeclarative"
cd "$PREFIX/build-qtdeclarative"

echo "[cmake] reconfigure qtdeclarative (qml_jit=OFF, BFREE_GUEST_FIXED_STACK)"
"$PREFIX/bin/qt-cmake" "$QT_SRC/qtdeclarative" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_CXX_FLAGS="-DBFREE_GUEST_FIXED_STACK" \
  -DQT_BUILD_TOOLS=OFF \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -DFEATURE_qml_profiler=OFF \
  -DFEATURE_qmlpreview=OFF \
  -DFEATURE_qml_jit=OFF

echo "[build] Qml QmlModels QmlMeta QmlCore Quick"
bash "$ROOT/tools/patch_qqmlimport_guest.sh" 2>/dev/null || true
bash "$ROOT/tools/patch_qv4engine_guest.sh" 2>/dev/null || true
cmake --build . --target Qml QmlModels QmlMeta QmlCore Quick --parallel "$JOBS"
cmake --install . || true
cp -an "$PREFIX/build-qtdeclarative/lib"/libQt6Qml*.a \
       "$PREFIX/build-qtdeclarative/lib"/libQt6Quick*.a \
       "$PREFIX/lib/" 2>/dev/null || true

echo "[verify] qml_jit and QQmlThread symbols"
grep -E 'FEATURE_qml_jit:BOOL|QT_FEATURE_qml_jit:INTERNAL' CMakeCache.txt | head -4 || true
if nm "$PREFIX/lib/libQt6Qml.a" 2>/dev/null | grep -q 'QQmlThreadPrivate11threadEvent'; then
  echo "[WARN] threaded QQmlThreadPrivate::threadEvent still present" >&2
else
  echo "[ok] no QQmlThreadPrivate::threadEvent (single-thread stub)"
fi
if nm "$PREFIX/lib/libQt6Qml.a" 2>/dev/null | grep -q 'BaselineJIT'; then
  echo "[WARN] QV4 BaselineJIT symbols still present" >&2
else
  echo "[ok] no BaselineJIT in libQt6Qml.a"
fi
ls -la "$PREFIX/lib/libQt6Qml.a" "$PREFIX/lib/libQt6Quick.a"
echo "[done] qtdeclarative single-thread rebuild complete"
