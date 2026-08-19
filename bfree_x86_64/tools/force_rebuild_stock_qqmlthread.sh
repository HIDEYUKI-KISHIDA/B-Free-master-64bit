#!/usr/bin/env bash
# Force-rebuild stock QQmlThread::isThisThread() into guest libQt6Qml.a.
# Use after reverting an always-true isThisThread() patch. cmake/ninja can
# print "no work to do" when qqmlthread.cpp.o is newer than the reverted .cpp.
#
#   export PATH="$HOME/xshim:$HOME/x86_64-elf-toolchain/bin:$PATH"
#   bash tools/force_rebuild_stock_qqmlthread.sh
#
# Does NOT apply the single-thread stub. Does NOT relink desktop.elf.

set -euo pipefail

if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi

QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
BUILD="$PREFIX/build-qtdeclarative"
SRC="$QT_SRC/qtdeclarative/src/qml/qml/ftw/qqmlthread.cpp"
OBJ="$BUILD/src/qml/CMakeFiles/Qml.dir/qml/ftw/qqmlthread.cpp.o"
ARCHIVE="$BUILD/lib/libQt6Qml.a"
PREFIX_A="$PREFIX/lib/libQt6Qml.a"

need() { command -v "$1" >/dev/null 2>&1 || { echo "[FAIL] missing: $1" >&2; exit 1; }; }
need cmake
need ninja

if [[ ! -f "$SRC" ]]; then
  echo "[FAIL] missing $SRC" >&2
  exit 1
fi
if [[ ! -d "$BUILD" ]]; then
  echo "[FAIL] missing $BUILD" >&2
  exit 1
fi
if grep -q 'bfree_isThisThread_always_true' "$SRC"; then
  echo "[FAIL] $SRC still has bfree_isThisThread_always_true" >&2
  exit 1
fi
if ! grep -q 'return d->isCurrentThread();' "$SRC"; then
  echo "[FAIL] $SRC isThisThread is not stock (expected return d->isCurrentThread())" >&2
  exit 1
fi

echo "[src] $SRC"
echo "[obj] $OBJ"
echo "[a]   $ARCHIVE"

rm -f "$OBJ" "$OBJ.d"
touch "$SRC"
cd "$BUILD"
echo "[build] forcing Qml (must compile qqmlthread.cpp.o)"
cmake --build . --target Qml -j"$(nproc)"
if [[ ! -f "$OBJ" ]]; then
  echo "[FAIL] $OBJ was not rebuilt" >&2
  exit 1
fi
if [[ ! -f "$ARCHIVE" ]]; then
  echo "[FAIL] $ARCHIVE missing after build" >&2
  exit 1
fi

# Unlinked .o tail-calls isCurrentThread as `jmp 0` + reloc. Plain
# `objdump -d` will not print the name; use -r. always-true is mov/xor+ret.
DUMP="$(objdump -d -r -C "$OBJ" 2>/dev/null || x86_64-elf-objdump -d -r -C "$OBJ")"
if ! printf '%s\n' "$DUMP" | grep -q 'isThisThread'; then
  echo "[FAIL] objdump has no isThisThread in $OBJ" >&2
  exit 1
fi
if printf '%s\n' "$DUMP" | awk '/isThisThread/{p=1} p&&/isCurrentThread/{found=1} p&&/^$/{exit} END{exit !found}'; then
  :
elif printf '%s\n' "$DUMP" | grep -A8 'isThisThread' | grep -Eq 'e9 00 00 00 00'; then
  echo "[ok] isThisThread is a reloc jmp (stock tail-call)"
else
  echo "[FAIL] rebuilt object looks like always-true isThisThread" >&2
  echo "$DUMP" | awk '/isThisThread/{p=1} p{print} p&&/ret/{c++; if(c>=2) exit}'
  exit 1
fi

cp -f "$ARCHIVE" "$PREFIX_A"
echo "[ok] copied $ARCHIVE -> $PREFIX_A"
ls -l --full-time "$OBJ" "$ARCHIVE" "$PREFIX_A"
echo "[next] relink desktop.elf against prefix lib/libQt6Qml.a"
