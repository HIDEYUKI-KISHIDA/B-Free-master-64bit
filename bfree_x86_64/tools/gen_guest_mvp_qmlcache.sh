#!/usr/bin/env bash
# Host-side: precompile GuestMvpShell_smoke.qml with qmlcachegen for guest.
#
# Usage (from repo root):
#   cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
#   export BFREE_QT_BUILD_DIR=/root/out/bfree-qt6-static
#   bash tools/gen_guest_mvp_qmlcache.sh
#
# If qmlcachegen is missing:
#   bash tools/build_host_qmlcachegen.sh
#
# Output: userland/desktop_qt/guest_mvp_shell_qmlcache.cpp

if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCAL_QMLCACHEGEN="$ROOT/tools/.qt-host-tools/libexec/qmlcachegen"
HOST_QT="${BFREE_QT_BUILD_DIR:-/root/out/bfree-qt6-static}"
HOST_QTDECL="${BFREE_QT_HOST_QTDECL_BUILD_DIR:-$HOST_QT/build-qtdeclarative}"
GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
GUEST_QTDECL="${GUEST_QT}/build-qtdeclarative"
SMOKE_QML="$ROOT/userland/desktop_qt/GuestMvpShell_smoke.qml"
OUT_DIR="$ROOT/userland/desktop_qt/.qmlcache"
OUT_CPP="$ROOT/userland/desktop_qt/guest_mvp_shell_qmlcache.cpp"
RESOURCE_PATH="qrc:/GuestMvpShell.qml"

find_qmlcachegen() {
  local p
  if [[ -n "${QMLCACHEGEN:-}" && -x "$QMLCACHEGEN" ]]; then
    echo "$QMLCACHEGEN"
    return 0
  fi
  for p in \
    "$GUEST_QTDECL/libexec/qmlcachegen" \
    "$GUEST_QTDECL/bin/qmlcachegen" \
    "$GUEST_QT/libexec/qmlcachegen" \
    "$GUEST_QT/bin/qmlcachegen" \
    "$LOCAL_QMLCACHEGEN" \
    "$HOST_QT/libexec/qmlcachegen" \
    "$HOST_QT/bin/qmlcachegen" \
    "$HOST_QTDECL/libexec/qmlcachegen" \
    "$HOST_QTDECL/bin/qmlcachegen" \
    /usr/lib/qt6/libexec/qmlcachegen \
    /usr/libexec/qmlcachegen; do
    if [[ -x "$p" ]]; then
      echo "$p"
      return 0
    fi
  done
  if command -v qmlcachegen >/dev/null 2>&1; then
    command -v qmlcachegen
    return 0
  fi
  find "$ROOT/tools/.qt-host-tools" "$HOST_QT" "$HOST_QTDECL" "$GUEST_QT" "$GUEST_QTDECL" \
    -maxdepth 8 -type f -name qmlcachegen -executable 2>/dev/null | head -1
}

QMLCACHEGEN="$(find_qmlcachegen || true)"

if [[ -z "$QMLCACHEGEN" || ! -x "$QMLCACHEGEN" ]]; then
  echo "[gen_guest_mvp_qmlcache] missing qmlcachegen (checked repo tools, host/guest prefix, PATH)" >&2
  echo "[gen_guest_mvp_qmlcache] HOST_QT=$HOST_QT  GUEST_QT=$GUEST_QT" >&2
  echo "[gen_guest_mvp_qmlcache] try (pick one):" >&2
  echo "  sudo apt install qt6-declarative-dev-tools && bash tools/build_host_qmlcachegen.sh" >&2
  echo "  sudo env BFREE_QT_GUEST_BUILD_DIR=/root/out/bfree-qt6-guest-static BFREE_QT_SRC=/root/src/qt6 bash tools/build_host_qmlcachegen.sh" >&2
  echo "  export QMLCACHEGEN=/path/to/qmlcachegen" >&2
  exit 1
fi

if [[ ! -f "$SMOKE_QML" ]]; then
  echo "[gen_guest_mvp_qmlcache] missing $SMOKE_QML" >&2
  exit 1
fi

mkdir -p "$OUT_DIR" "$(dirname "$OUT_CPP")"
echo "[gen_guest_mvp_qmlcache] using $QMLCACHEGEN"

if "$QMLCACHEGEN" --bare --resource-path "$RESOURCE_PATH" -o "$OUT_CPP" "$SMOKE_QML"; then
  echo "[gen_guest_mvp_qmlcache] wrote $OUT_CPP (bare mode)"
  exit 0
fi

# Older/custom qmlcachegen flags (best-effort fallback).
if "$QMLCACHEGEN" \
  --resource-name GuestMvpShell \
  --resource-path "$RESOURCE_PATH" \
  --out-dir "$OUT_DIR" \
  "$SMOKE_QML"; then
  GEN="$(find "$OUT_DIR" -name '*_qml.cpp' | head -1)"
  if [[ -n "$GEN" ]]; then
    cp -f "$GEN" "$OUT_CPP"
    echo "[gen_guest_mvp_qmlcache] wrote $OUT_CPP (from $GEN)"
    exit 0
  fi
fi

echo "[gen_guest_mvp_qmlcache] qmlcachegen failed; run:" >&2
echo "  $QMLCACHEGEN --help" >&2
exit 1
