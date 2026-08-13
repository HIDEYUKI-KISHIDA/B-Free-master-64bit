#!/usr/bin/env bash
# Show guest qtbase wayland rebuild progress (configure / build / install).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
BD="$PREFIX/build-qtbase"
FEATURES="$PREFIX/lib/cmake/Qt6Gui/Qt6GuiFeatures.cmake"
CACHE="$BD/CMakeCache.txt"
LOG="$BD/configure.log"

echo "=== guest qtbase wayland status ==="
echo "  prefix: $PREFIX"
echo "  build:  $BD"
echo ""

if pgrep -af 'cmake.*qtbase|ninja.*build-qtbase' >/dev/null 2>&1; then
  echo "[running] build processes:"
  pgrep -af 'cmake.*qtbase|ninja.*build-qtbase' || true
  echo ""
else
  echo "[running] no cmake/ninja for build-qtbase (idle or finished)"
  echo ""
fi

if [[ -f "$FEATURES" ]]; then
  echo "[installed] $FEATURES"
  grep QT_FEATURE_wayland "$FEATURES" || echo "  (no QT_FEATURE_wayland line)"
  echo ""
fi

if [[ -f "$CACHE" ]]; then
  echo "[configure] CMakeCache.txt exists"
  grep -E '^FEATURE_wayland:|^QT_FEATURE_wayland:' "$CACHE" 2>/dev/null || echo "  wayland feature not in cache yet"
  if [[ -f "$BD/build.ninja" ]]; then
    echo "  build.ninja: yes (configure finished)"
  else
    echo "  build.ninja: no (configure still running or failed)"
  fi
  echo ""
fi

if [[ -f "$LOG" ]]; then
  echo "[configure.log] last 5 lines:"
  tail -5 "$LOG"
  echo ""
fi

if [[ -f "$BD/.ninja_log" ]]; then
  local_done="$(wc -l <"$BD/.ninja_log" 2>/dev/null || echo 0)"
  echo "[ninja] .ninja_log lines: $local_done (grows while compiling)"
  tail -3 "$BD/.ninja_log" 2>/dev/null || true
  echo ""
fi

if [[ -f "$PREFIX/lib/libQt6Gui.a" ]]; then
  echo "[artifact] libQt6Gui.a: $(ls -lh "$PREFIX/lib/libQt6Gui.a" | awk '{print $5, $6, $7, $8}')"
fi

echo ""
echo "Resume build (if configure done):"
echo "  JOBS=4 bash tools/resume_guest_qtbase_wayland_build.sh"
