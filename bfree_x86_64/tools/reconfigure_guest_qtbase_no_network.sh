#!/usr/bin/env bash
# Reconfigure an in-progress guest qtbase build without wiping Core (disable Network/ssl).
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail

PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
BD="$PREFIX/build-qtbase"

if [[ ! -f "$BD/CMakeCache.txt" ]]; then
  echo "[reconfig] missing $BD/CMakeCache.txt — run tools/rebuild_guest_qt_minimal.sh first" >&2
  exit 1
fi

cd "$BD"
echo "[reconfig] disabling Network/ssl/brotli (avoids host /usr/include glibc headers)"
cmake . \
  -DINPUT_openssl=no \
  -DFEATURE_network=OFF \
  -DFEATURE_ssl=OFF \
  -DFEATURE_brotli=OFF \
  -DFEATURE_glib=OFF

echo "[reconfig] resume build (do not cmake --install until ninja succeeds)"
cmake --build . --parallel "${JOBS:-2}"
