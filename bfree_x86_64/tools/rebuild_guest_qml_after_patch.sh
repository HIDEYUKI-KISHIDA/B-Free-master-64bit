#!/usr/bin/env bash
# Rebuild libQt6Qml.a after guest qqmltypedata.cpp / qqmlmetatype.cpp patches.
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
BD="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}/build-qtdeclarative"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/qml/qqmltypedata.cpp.o" \
      "$BD/src/qml/CMakeFiles/Qml.dir/qml/qqmlmetatype.cpp.o"
cmake --build "$BD" --target Qml -j"$(nproc 2>/dev/null || echo 8)"
cp -a "$BD/lib/libQt6Qml.a" "$PREFIX/lib/"
ls -la "$PREFIX/lib/libQt6Qml.a"
echo "[rebuild_guest_qml] done"
