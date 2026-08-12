#!/usr/bin/env bash
# Fetch Qt 6 super-module sources for B3 (run once; ~2–5 GB download).
# Prefer Linux home dir (NOT /mnt/c) for speed: export BFREE_QT_SRC=$HOME/src/qt6
#
#   bash tools/clone_qt6_src.sh
#   bash tools/build_bfree_qt6_guest.sh

set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
QT_TAG="${BFREE_QT_VERSION:-6.8.0}"
QT_GIT="${BFREE_QT_GIT_URL:-https://code.qt.io/qt/qt5.git}"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1" >&2; exit 1; }; }
need git

if [[ "$QT_SRC" == /mnt/* ]]; then
  echo "[clone_qt6] WARN: Qt on /mnt/c is slow. Prefer: export BFREE_QT_SRC=\$HOME/src/qt6"
fi

mkdir -p "$(dirname "$QT_SRC")"

if [[ -d "$QT_SRC/.git" ]]; then
  echo "[clone_qt6] already exists: $QT_SRC (fetch + checkout $QT_TAG)"
  git -C "$QT_SRC" fetch --tags origin
  git -C "$QT_SRC" checkout "v${QT_TAG}" 2>/dev/null || git -C "$QT_SRC" checkout "$QT_TAG"
else
  echo "[clone_qt6] cloning $QT_GIT -> $QT_SRC (shallow; may take a while)"
  git clone --branch "v${QT_TAG}" --depth 1 "$QT_GIT" "$QT_SRC" 2>/dev/null \
    || git clone --branch "$QT_TAG" --depth 1 "$QT_GIT" "$QT_SRC" 2>/dev/null \
    || { git clone "$QT_GIT" "$QT_SRC" && git -C "$QT_SRC" checkout "v${QT_TAG}" || git -C "$QT_SRC" checkout "$QT_TAG"; }
fi

echo "[clone_qt6] init-repository (qtbase, qtdeclarative, qtshadertools)..."
cd "$QT_SRC"
QT_MODULES="qtbase,qtdeclarative,qtshadertools"
if [[ "${BFREE_QT_WAYLAND:-0}" == "1" ]]; then
  QT_MODULES="${QT_MODULES},qtwayland"
fi
if [[ -x ./init-repository ]]; then
  ./init-repository --module-subset="$QT_MODULES"
elif [[ -f init-repository ]]; then
  perl init-repository --module-subset="$QT_MODULES"
else
  echo "[clone_qt6] ERROR: init-repository not found in $QT_SRC"
  exit 1
fi

if [[ "${BFREE_QT_WAYLAND:-0}" == "1" ]]; then
  echo "[clone_qt6] qtwayland included (BFREE_QT_WAYLAND=1)"
else
  echo "[clone_qt6] tip: compositor path needs qtwayland — run tools/build_guest_qtwayland.sh (auto-fetch) or re-clone with BFREE_QT_WAYLAND=1"
fi

echo "[clone_qt6] done. Next:"
echo "  export BFREE_QT_SRC=$QT_SRC"
echo "  export BFREE_QT_BUILD_DIR=\${BFREE_QT_BUILD_DIR:-\$HOME/out/bfree-qt6-static}"
echo "  bash tools/build_bfree_qt6_guest.sh"
