#!/usr/bin/env bash
set -euo pipefail
GUEST="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
BD="$GUEST/build-qtdeclarative"
test -d "$BD" || { echo "missing $BD" >&2; exit 1; }
mkdir -p "$GUEST/lib" "$GUEST/mkspecs/modules" "$GUEST/lib/cmake"
cp -an "$BD/lib"/libQt6Qml*.a "$BD/lib"/libQt6Quick*.a "$GUEST/lib/" 2>/dev/null || true
cp -an "$BD/mkspecs/modules"/qt_lib_qml*.pri "$BD/mkspecs/modules"/qt_lib_quick*.pri "$GUEST/mkspecs/modules/" 2>/dev/null || true
test -d "$BD/lib/cmake/Qt6Qml" && rm -rf "$GUEST/lib/cmake/Qt6Qml" && cp -a "$BD/lib/cmake/Qt6Qml" "$GUEST/lib/cmake/"
test -d "$BD/lib/cmake/Qt6QmlModels" && rm -rf "$GUEST/lib/cmake/Qt6QmlModels" && cp -a "$BD/lib/cmake/Qt6QmlModels" "$GUEST/lib/cmake/"
test -d "$BD/lib/cmake/Qt6QmlMeta" && rm -rf "$GUEST/lib/cmake/Qt6QmlMeta" && cp -a "$BD/lib/cmake/Qt6QmlMeta" "$GUEST/lib/cmake/"
test -d "$BD/lib/cmake/Qt6QmlCore" && rm -rf "$GUEST/lib/cmake/Qt6QmlCore" && cp -a "$BD/lib/cmake/Qt6QmlCore" "$GUEST/lib/cmake/"
test -d "$BD/lib/cmake/Qt6Quick" && rm -rf "$GUEST/lib/cmake/Qt6Quick" && cp -a "$BD/lib/cmake/Qt6Quick" "$GUEST/lib/cmake/"
test -d "$BD/lib/cmake/Qt6QuickTemplates2" && rm -rf "$GUEST/lib/cmake/Qt6QuickTemplates2" && cp -a "$BD/lib/cmake/Qt6QuickTemplates2" "$GUEST/lib/cmake/"
test -f "$GUEST/mkspecs/modules/qt_lib_quick.pri" || { echo "missing qt_lib_quick.pri" >&2; exit 1; }
test -f "$GUEST/lib/libQt6Quick.a" || { echo "missing libQt6Quick.a" >&2; exit 1; }
echo "[sync-qtdecl] OK"
ls -la "$GUEST/mkspecs/modules/qt_lib_quick.pri" "$GUEST/lib/libQt6Quick.a"
