#!/usr/bin/env bash
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
# Stage DesktopShell.qml tree into iso_root for guest Qt (B5).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INTGUI="$ROOT/gui_server/integration_gui"
STAGE="$ROOT/iso_root/bfree/qml"
MVP_QML="$INTGUI/GuestMvpShell.qml"
MAIN_QML="$INTGUI/DesktopShell.qml"

mkdir -p "$STAGE"

copy_tree() {
  local src="$1"
  local dst="$2"
  if [[ -d "$src" ]]; then
    mkdir -p "$dst"
    cp -a "$src/." "$dst/"
  fi
}

echo "[stage_mvp_qml] -> $STAGE"
cp -f "$MVP_QML" "$STAGE/"
cp -f "$MAIN_QML" "$STAGE/"
copy_tree "$INTGUI/wabi_components" "$STAGE/wabi_components"
copy_tree "$INTGUI/kde_widgets" "$STAGE/kde_widgets"
copy_tree "$INTGUI/kde_themes" "$STAGE/kde_themes"

if [[ -f "$INTGUI/third_party/breeze-icons/index.theme" ]]; then
  copy_tree "$INTGUI/third_party/breeze-icons" "$STAGE/third_party/breeze-icons"
else
  echo "[stage_mvp_qml] WARN: breeze-icons not synced (optional for MVP)"
fi

echo "[stage_mvp_qml] staged GuestMvpShell.qml + DesktopShell.qml + imports"
