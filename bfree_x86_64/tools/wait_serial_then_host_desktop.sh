#!/usr/bin/env bash
# 実機ブート時: ゲストのシリアルログをファイルで受け取り、マーカー検出後にホスト DesktopShell を起動する。
# QEMU は使わない。開発 PC（WSL/Linux + DISPLAY または WSLg）で実行する。
#
# 例（実機 COM を USB シリアルで開発 PC に接続し、ログをファイルへ）:
#   stty -F /dev/ttyUSB0 115200 raw -echo
#   cat /dev/ttyUSB0 > /tmp/bfree-physical.serial &
#   BFREE_GUEST_SERIAL_LOG=/tmp/bfree-physical.serial \
#     bash tools/wait_serial_then_host_desktop.sh
#
# 環境変数は build_and_run_desktop_shell.sh と同じ（BFREE_GUEST_HOST_GUI_WAIT_LINE 等）。

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="${BFREE_GUEST_SERIAL_LOG:-}"
WAIT_LINE="${BFREE_GUEST_HOST_GUI_WAIT_LINE:-[init] login: OK}"
TIMEOUT="${BFREE_GUEST_LOGIN_WAIT_TIMEOUT_SEC:-600}"
INTGUI="$ROOT/gui_server/integration_gui"

if [[ -z "$LOG" ]]; then
  echo "[wait_serial_host] ERROR: set BFREE_GUEST_SERIAL_LOG to a serial capture file." >&2
  echo "  Example: cat /dev/ttyUSB0 > /tmp/bfree-physical.serial &" >&2
  exit 1
fi

touch "$LOG" 2>/dev/null || true

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  echo "[wait_serial_host] ERROR: DISPLAY or WAYLAND_DISPLAY required for host DesktopShell." >&2
  exit 1
fi

export BFREE_GUI_QPA="${BFREE_GUI_QPA:-xcb}"
if [[ -n "${WAYLAND_DISPLAY:-}" && -z "${DISPLAY:-}" ]]; then
  export BFREE_GUI_QPA="${BFREE_GUI_QPA:-wayland}"
fi

echo "[wait_serial_host] log=$LOG marker=$WAIT_LINE timeout=${TIMEOUT}s"
deadline=$((SECONDS + TIMEOUT))
while (( SECONDS < deadline )); do
  if grep -qF "$WAIT_LINE" "$LOG" 2>/dev/null; then
    echo "[wait_serial_host] marker seen — starting host DesktopShell.qml"
    cd "$INTGUI"
    export BFREE_GUI_QML=DesktopShell.qml
    export BFREE_GUI_TIMEOUT_MS="${BFREE_HOST_GUI_TIMEOUT_MS:-0}"
    export BFREE_GUI_ALLOW_WAIT=1
    exec bash ./build_and_run_bfree_gui.sh
  fi
  sleep 0.25
done

echo "[wait_serial_host] WARN: timeout — starting host DesktopShell anyway"
cd "$INTGUI"
export BFREE_GUI_QML=DesktopShell.qml
export BFREE_GUI_TIMEOUT_MS="${BFREE_HOST_GUI_TIMEOUT_MS:-0}"
export BFREE_GUI_ALLOW_WAIT=1
exec bash ./build_and_run_bfree_gui.sh
