#!/usr/bin/env bash
# Resume guest qtbase wayland rebuild after configure (or retry build+install).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
BD="$PREFIX/build-qtbase"
JOBS="${JOBS:-4}"
export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"

log_phase() { echo "[$(date '+%H:%M:%S')] $*"; }

if [[ ! -f "$BD/CMakeCache.txt" ]]; then
  echo "[resume] ERROR: no $BD/CMakeCache.txt — run rebuild_guest_qtbase_wayland_only.sh first" >&2
  exit 1
fi

if ! grep -qE '^FEATURE_wayland:BOOL=ON$|^QT_FEATURE_wayland:BOOL=ON$' "$BD/CMakeCache.txt"; then
  echo "[resume] ERROR: FEATURE_wayland not ON in CMakeCache.txt" >&2
  grep -E 'wayland|Wayland' "$BD/CMakeCache.txt" | head -10 >&2 || true
  echo "  re-run: BFREE_AUTO_CONFIRM=1 bash $ROOT/tools/rebuild_guest_qtbase_wayland_only.sh" >&2
  exit 1
fi

if [[ ! -f "$BD/build.ninja" ]]; then
  echo "[resume] ERROR: configure incomplete (no build.ninja). Wait or check configure.log" >&2
  tail -20 "$BD/configure.log" 2>&1 || true
  exit 1
fi

# Block host /usr/include; ensure libgcc includes + POSIX limit macros for -nostdinc++.
if ! grep -q nostdinc "$BD/toolchain.cmake" 2>/dev/null \
   || ! grep -q '_POSIX_PIPE_BUF=512' "$BD/toolchain.cmake" 2>/dev/null; then
  echo "[resume] patching toolchain (nostdinc / POSIX limits) ..."
  bash "$ROOT/tools/fix_guest_qtbase_nostdinc.sh"
fi

bash "$ROOT/tools/patch_qt_guest_qsharedmemory_path_max.sh"

log_phase "building qtbase (jobs=$JOBS) — typically 1–3 hours on WSL ..."
cmake --build "$BD" --parallel "$JOBS" 2>&1 | tee -a "$BD/build.log"

log_phase "installing qtbase ..."
cmake --install "$BD"

if [[ -f "$PREFIX/lib/cmake/Qt6Gui/Qt6GuiFeatures.cmake" ]]; then
  log_phase "Qt6Gui features:"
  grep QT_FEATURE_wayland "$PREFIX/lib/cmake/Qt6Gui/Qt6GuiFeatures.cmake" || true
fi

log_phase "done"
