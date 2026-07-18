#!/usr/bin/env bash
# v329: patch qqmlimport (qrc-only) + rebuild libQt6Qml.a with BFREE_GUEST_FIXED_STACK.
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
QT_SRC="${BFREE_QT_SRC:-/root/src/qt6}"
BD="$PREFIX/build-qtdeclarative"
JOBS="${JOBS:-4}"

cd "$ROOT"
sed -i 's/\r$//' tools/patch_qqmlimport_guest.sh tools/v329_rebuild.sh 2>/dev/null || true

echo "[v329] patch qqmlimport (qrc-only import paths)"
bash tools/patch_qqmlimport_guest.sh

if [[ ! -d "$BD" ]]; then
  echo "[v329] ERROR: missing $BD — run tools/rebuild_guest_qtdeclarative_singlethread.sh first" >&2
  exit 1
fi

echo "[v329] reconfigure qtdeclarative with -DBFREE_GUEST_FIXED_STACK"
cd "$BD"
"$PREFIX/bin/qt-cmake" "$QT_SRC/qtdeclarative" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_CXX_FLAGS="-DBFREE_GUEST_FIXED_STACK" \
  -DQT_BUILD_TOOLS=OFF \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -DFEATURE_qml_profiler=OFF \
  -DFEATURE_qmlpreview=OFF \
  -DFEATURE_qml_jit=OFF

echo "[v329] rebuild Qml (qqmlimport.cpp)"
rm -f src/qml/CMakeFiles/Qml.dir/qml/qqmlimport.cpp.o
cmake --build . --target Qml -j"$JOBS"
cp -a "$BD/lib/libQt6Qml.a" "$PREFIX/lib/"

echo "[v329] verify patch in libQt6Qml.a"
if strings "$PREFIX/lib/libQt6Qml.a" | grep -q 'qrc:/qt/qml'; then
  echo "[v329] ok: qrc import path string present"
else
  echo "[v329] WARN: qrc:/qt/qml not found in libQt6Qml.a" >&2
fi

cd "$ROOT"
sed -i 's/mmap96-v[0-9]*/mmap96-v329/' userland/desktop_qt/guest_main.cpp
rm -f userland/desktop_qt/desktop.elf
bash tools/build_guest_desktop_elf.sh
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
export BFREE_QT_GUEST_LINKED=1 BFREE_AUTO_LOGIN=1 BFREE_MVP_GUEST_QML=1
bash build.sh
strings userland/desktop_qt/desktop.elf | grep 'mmap96-v' | head -1
echo "[v329] done"
