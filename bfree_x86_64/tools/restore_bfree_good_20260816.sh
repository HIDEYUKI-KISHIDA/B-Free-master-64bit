#!/usr/bin/env bash
# Restore the known-good FB desktop (2026-08-16 VA 0x62c4160).
set -euo pipefail

GOOD="${BFREE_GOOD_DIR:-$HOME/out/bfree-good-20260816}"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"

test -f "$GOOD/libQt6Qml.a"
test -f "$GOOD/desktop.elf"
test -f "$GOOD/guest_resource_holder_va.h"

cp -f "$GOOD/libQt6Qml.a" "$PREFIX/lib/libQt6Qml.a"
cp -f "$GOOD/libQt6Qml.a" "$PREFIX/build-qtdeclarative/lib/libQt6Qml.a"
cp -f "$GOOD/desktop.elf" "$DESK/desktop.elf"
cp -f "$GOOD/desktop.elf" "$DESK/desktop"
cp -f "$GOOD/guest_resource_holder_va.h" "$DESK/guest_resource_holder_va.h"
echo "[ok] restored $GOOD -> prefix + $DESK"
ls -l --full-time "$PREFIX/lib/libQt6Qml.a" "$DESK/desktop.elf" "$DESK/guest_resource_holder_va.h"
