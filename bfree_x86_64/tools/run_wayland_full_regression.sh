#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

RUNTIME_SEC_SERVER_ONLY="${RUNTIME_SEC_SERVER_ONLY:-12}"
RUNTIME_SEC_CLIENT="${RUNTIME_SEC_CLIENT:-20}"
SKIP_BUILD="${SKIP_BUILD:-0}"

QML_SMOKE_FILE="$ROOT_DIR/tools/qml_wayland_smoke.qml"

echo "[regression] root: $ROOT_DIR"

if [[ "$SKIP_BUILD" != "1" ]]; then
  echo "[regression] build gui_server"
  make -C "$ROOT_DIR/gui_server" clean
  make -C "$ROOT_DIR/gui_server"
else
  echo "[regression] skip build (SKIP_BUILD=1)"
fi

echo "[regression] strict check (server only; expected to fail without client traces)"
set +e
RUNTIME_SEC="$RUNTIME_SEC_SERVER_ONLY" \
  bash "$ROOT_DIR/gui_server/integration_gui/run_wayland_real_compat_check.sh"
SERVER_ONLY_RC=$?
set -e
if [[ $SERVER_ONLY_RC -eq 0 ]]; then
  echo "[regression] server-only strict: PASS"
else
  echo "[regression] server-only strict: FAIL (expected in headless/no-client path)"
fi

echo "[regression] strict check with wayland-info"
CLIENT_CMD="wayland-info" \
RUNTIME_SEC="$RUNTIME_SEC_CLIENT" \
  bash "$ROOT_DIR/gui_server/integration_gui/run_wayland_real_compat_with_client.sh"

if [[ ! -f "$QML_SMOKE_FILE" ]]; then
  echo "[regression] create qml smoke file: $QML_SMOKE_FILE"
  cat > "$QML_SMOKE_FILE" <<'EOF'
import QtQuick 2.15

Rectangle {
    width: 320
    height: 200
    color: "#202830"

    Text {
        anchors.centerIn: parent
        text: "B-Free Wayland Smoke"
        color: "white"
    }
}
EOF
fi

echo "[regression] strict check with qmlscene (Wayland)"
CLIENT_CMD="QT_QPA_PLATFORM=wayland qmlscene $QML_SMOKE_FILE" \
RUNTIME_SEC="$RUNTIME_SEC_CLIENT" \
  bash "$ROOT_DIR/gui_server/integration_gui/run_wayland_real_compat_with_client.sh"

echo "[regression] regenerate coverage"
python3 "$ROOT_DIR/tools/wayland_core_bfree_coverage.py"

echo "[regression] done"
