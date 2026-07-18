#!/usr/bin/env bash
# Ensure a runnable host qmlcachegen for gen_guest_mvp_qmlcache.sh.
#
# Installs to repo-local tools/.qt-host-tools/libexec/qmlcachegen (no /root write needed).
# Falls back across host Qt prefix, guest Qt prefix, then apt.
#
# Usage (from repo root):
#   cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
#   export BFREE_QT_GUEST_BUILD_DIR=/root/out/bfree-qt6-guest-static
#   export BFREE_QT_SRC=/root/src/qt6
#   sudo env BFREE_QT_GUEST_BUILD_DIR=/root/out/bfree-qt6-guest-static BFREE_QT_SRC=/root/src/qt6 bash tools/build_host_qmlcachegen.sh
#
# Easiest when no custom Qt tools exist:
#   sudo apt install qt6-declarative-dev-tools
#   bash tools/build_host_qmlcachegen.sh

if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCAL_QMLCACHEGEN="$ROOT/tools/.qt-host-tools/libexec/qmlcachegen"
QT_SRC="${BFREE_QT_SRC:-/root/src/qt6}"
JOBS="${BFREE_QT_BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}"
QT_PREFIX=""
QTDECL_BD=""

install_local() {
  local src="$1"
  mkdir -p "$(dirname "$LOCAL_QMLCACHEGEN")"
  cp -f "$src" "$LOCAL_QMLCACHEGEN"
  chmod +x "$LOCAL_QMLCACHEGEN"
  echo "[build_host_qmlcachegen] ready: $LOCAL_QMLCACHEGEN"
}

find_existing_qmlcachegen() {
  local p
  for p in \
    "$LOCAL_QMLCACHEGEN" \
    "${QMLCACHEGEN:-}" \
    "${BFREE_QT_BUILD_DIR:-}/libexec/qmlcachegen" \
    "${BFREE_QT_BUILD_DIR:-}/bin/qmlcachegen" \
    "${BFREE_QT_GUEST_BUILD_DIR:-}/libexec/qmlcachegen" \
    "${BFREE_QT_GUEST_BUILD_DIR:-}/bin/qmlcachegen" \
    /root/out/bfree-qt6-static/libexec/qmlcachegen \
    /root/out/bfree-qt6-guest-static/libexec/qmlcachegen \
    /usr/lib/qt6/libexec/qmlcachegen \
    /usr/libexec/qmlcachegen; do
    [[ -n "$p" && -x "$p" ]] || continue
    echo "$p"
    return 0
  done
  if command -v qmlcachegen >/dev/null 2>&1; then
    command -v qmlcachegen
    return 0
  fi
  return 1
}

qt_prefix_ok() {
  local p="$1"
  [[ -n "$p" && -d "$p" ]] || return 1
  [[ -r "$p/lib/libQt6Core.a" || -r "$p/lib/libQt6Core.so" ]] || return 1
  [[ -x "$p/bin/qt-cmake" ]] || return 1
}

resolve_qt_prefix() {
  local p
  for p in \
    "${BFREE_QT_BUILD_DIR:-}" \
    /root/out/bfree-qt6-static \
    "${BFREE_QT_GUEST_BUILD_DIR:-}" \
    /root/out/bfree-qt6-guest-static \
    "${HOME}/out/bfree-qt6-static"; do
    if qt_prefix_ok "$p"; then
      echo "$p"
      return 0
    fi
  done
  return 1
}

EXISTING="$(find_existing_qmlcachegen || true)"
if [[ -n "$EXISTING" && -x "$EXISTING" ]]; then
  if [[ "$EXISTING" != "$LOCAL_QMLCACHEGEN" ]]; then
    install_local "$EXISTING"
  else
    echo "[build_host_qmlcachegen] ready: $LOCAL_QMLCACHEGEN"
  fi
  exit 0
fi

if [[ -x /usr/lib/qt6/libexec/qmlcachegen ]]; then
  echo "[build_host_qmlcachegen] using apt qmlcachegen (/usr/lib/qt6/libexec/qmlcachegen)" >&2
  echo "[build_host_qmlcachegen] note: apt Qt may differ from guest Qt 6.8; prefer custom build if load fails" >&2
  install_local /usr/lib/qt6/libexec/qmlcachegen
  exit 0
fi

if command -v apt-get >/dev/null 2>&1 && ! dpkg -s qt6-declarative-dev-tools >/dev/null 2>&1; then
  echo "[build_host_qmlcachegen] no qmlcachegen found" >&2
  echo "[build_host_qmlcachegen] quickest fix:" >&2
  echo "  sudo apt install qt6-declarative-dev-tools" >&2
  echo "  bash tools/build_host_qmlcachegen.sh" >&2
  echo "[build_host_qmlcachegen] or build from your Qt tree (needs readable prefix + qt-cmake):" >&2
  echo "  sudo env BFREE_QT_GUEST_BUILD_DIR=/root/out/bfree-qt6-guest-static BFREE_QT_SRC=/root/src/qt6 bash tools/build_host_qmlcachegen.sh" >&2
fi

QT_PREFIX="$(resolve_qt_prefix || true)"
if [[ -z "$QT_PREFIX" ]]; then
  echo "[build_host_qmlcachegen] no Qt prefix with libQt6Core + qt-cmake found" >&2
  echo "[build_host_qmlcachegen] checked:" >&2
  echo "  BFREE_QT_BUILD_DIR=${BFREE_QT_BUILD_DIR:-<unset>}" >&2
  echo "  BFREE_QT_GUEST_BUILD_DIR=${BFREE_QT_GUEST_BUILD_DIR:-<unset>}" >&2
  echo "  /root/out/bfree-qt6-static" >&2
  echo "  /root/out/bfree-qt6-guest-static" >&2
  echo "[build_host_qmlcachegen] diagnose (as root):" >&2
  echo "  ls -la /root/out/bfree-qt6-guest-static/lib/libQt6Core.a" >&2
  echo "  ls -la /root/out/bfree-qt6-guest-static/bin/qt-cmake" >&2
  exit 1
fi

QTDECL_BD="${BFREE_QT_HOST_QTDECL_BUILD_DIR:-$QT_PREFIX/build-qtdeclarative}"
echo "[build_host_qmlcachegen] Qt prefix: $QT_PREFIX"
echo "[build_host_qmlcachegen] qtdeclarative build: $QTDECL_BD"

if [[ ! -f "$QT_SRC/qtdeclarative/CMakeLists.txt" ]]; then
  echo "[build_host_qmlcachegen] qtdeclarative sources missing: $QT_SRC/qtdeclarative" >&2
  exit 1
fi

command -v cmake >/dev/null 2>&1 || { echo "[build_host_qmlcachegen] missing cmake" >&2; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "[build_host_qmlcachegen] missing ninja" >&2; exit 1; }

if [[ ! -f "$QTDECL_BD/CMakeCache.txt" ]]; then
  echo "[build_host_qmlcachegen] configuring $QTDECL_BD ..."
  mkdir -p "$QTDECL_BD"
  (
    cd "$QTDECL_BD"
    "$QT_PREFIX/bin/qt-cmake" "$QT_SRC/qtdeclarative" \
      -DCMAKE_INSTALL_PREFIX="$QT_PREFIX" \
      -DQT_BUILD_EXAMPLES=OFF \
      -DQT_BUILD_TESTS=OFF \
      -DQT_BUILD_TOOLS=ON \
      -DFEATURE_qml_profiler=OFF
  )
fi

echo "[build_host_qmlcachegen] building qmlcachegen (jobs=$JOBS) ..."
cmake --build "$QTDECL_BD" --target qmlcachegen --parallel "$JOBS"

BUILT="$(find "$QTDECL_BD" -maxdepth 10 -type f -name qmlcachegen -executable 2>/dev/null | head -1)"
if [[ -z "$BUILT" || ! -x "$BUILT" ]]; then
  echo "[build_host_qmlcachegen] build finished but qmlcachegen not found under $QTDECL_BD" >&2
  echo "[build_host_qmlcachegen] try: find $QTDECL_BD -name qmlcachegen" >&2
  exit 1
fi

install_local "$BUILT"
