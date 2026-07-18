#!/usr/bin/env bash
# Generate userland/desktop_qt/guest_desktop.qrc (DesktopShell.qml tree for guest ISO).
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
set -eu
set -o pipefail
if [[ -z "${BFREE_ROOT:-}" ]]; then
  _self="${BASH_SOURCE[0]:-$0}"
  if [[ "$_self" == bash || "$_self" == -bash || "$_self" == /*bash ]]; then
    echo "[gen_guest_desktop_qrc] ERROR: export BFREE_ROOT=/path/to/bfree_x86_64" >&2
    exit 1
  fi
  ROOT="$(cd "$(dirname "$_self")/.." && pwd)"
else
  ROOT="$(cd "$BFREE_ROOT" && pwd)"
fi
export BFREE_ROOT="$ROOT"
INTGUI="$ROOT/gui_server/integration_gui"
OUT="$ROOT/userland/desktop_qt/guest_desktop.qrc"
REL="../../gui_server/integration_gui"
mkdir -p "$(dirname "$OUT")"

qrc_file() {
  local alias="$1"
  local relpath="$2"
  echo "        <file alias=\"${alias}\">${REL}/${relpath}</file>"
}

{
  echo '<!DOCTYPE RCC>'
  echo '<RCC version="1.0">'
  echo '    <qresource prefix="/">'
  qrc_file "DesktopShell.qml" "DesktopShell.qml"
  qrc_file "GuestMvpShell.qml" "GuestMvpShell.qml"
  qrc_file "TK2ControlPanel.qml" "TK2ControlPanel.qml"

  while IFS= read -r -d '' f; do
    base="${f#"$INTGUI/"}"
    qrc_file "$base" "$base"
  done < <(find "$INTGUI/wabi_components" "$INTGUI/kde_widgets" "$INTGUI/kde_themes" \
    -type f \( -name '*.qml' -o -name '*.json' -o -name 'qmldir' \) -print0 2>/dev/null | sort -z || true)

  # Breeze: only trash + file manager icons (no find|head — avoids exit 141 on /mnt/c).
  BREEZE="$INTGUI/third_party/breeze-icons"
  if [[ -d "$BREEZE" ]]; then
    qrc_file "third_party/breeze-icons/index.theme" "third_party/breeze-icons/index.theme"
    for ic in places/48/user-trash.svg places/48/user-trash-full.svg \
              apps/48/system-file-manager.svg apps/48/utilities-terminal.svg; do
      if [[ -f "$BREEZE/$ic" ]]; then
        qrc_file "third_party/breeze-icons/$ic" "third_party/breeze-icons/$ic"
      fi
    done
  fi

  echo '    </qresource>'
  echo '</RCC>'
} >"$OUT"

echo "[gen_guest_desktop_qrc] -> $OUT ($(wc -l <"$OUT") lines)"
