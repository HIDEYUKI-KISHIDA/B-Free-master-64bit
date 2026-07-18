#!/usr/bin/env bash
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
set -euo pipefail
ROOT="${BFREE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SRC="${BFREE_QT_SRC:-/root/src/qt6}/qtdeclarative/src/qml/memory/qv4stacklimits.cpp"
BD="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}/build-qtdeclarative"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
FLAGS="$BD/src/qml/CMakeFiles/Qml.dir/flags.make"

cp "$ROOT/gui_server/integration_gui/qtdeclarative/src/qml/memory/qv4stacklimits.cpp" "$SRC"
if ! grep -q BFREE_GUEST_FIXED_STACK "$FLAGS"; then
  sed -i 's/CXX_DEFINES = /CXX_DEFINES = -DBFREE_GUEST_FIXED_STACK /' "$FLAGS"
fi
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/memory/qv4stacklimits.cpp.o"
cmake --build "$BD" --target Qml -j"$(nproc)"
cp -a "$BD/lib/libQt6Qml.a" "$PREFIX/lib/"
echo "[rebuild-qml-stack] libQt6Qml.a updated"
