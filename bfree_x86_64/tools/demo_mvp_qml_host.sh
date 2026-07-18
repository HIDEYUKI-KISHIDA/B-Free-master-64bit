#!/usr/bin/env bash
# Visual MVP proof on HOST (WSL/Linux Qt) — full Window UI from iso_root GuestMvpShell.qml.
# Guest desktop.elf is unchanged; this proves the MVP QML itself while guest compile work continues.
#
# Usage:
#   cd Program/bfree_x86_64 && bash tools/demo_mvp_qml_host.sh
#
# Requires: host Qt6 Qml/Quick (see build_and_run_bfree_gui.sh checks).

set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

MVP_QML="$ROOT/iso_root/bfree/qml/GuestMvpShell.qml"
if [[ ! -f "$MVP_QML" ]]; then
  echo "[demo_mvp_host] missing $MVP_QML" >&2
  exit 1
fi

GUI="$ROOT/gui_server/integration_gui"
if [[ -x "$GUI/run_desktop_shell.sh" ]]; then
  echo "[demo_mvp_host] launching integration_gui DesktopShell runner (GuestMvpShell on host)..."
  exec bash "$GUI/run_desktop_shell.sh" "$MVP_QML"
fi

if [[ -x "$ROOT/build_and_run_bfree_gui.sh" ]]; then
  echo "[demo_mvp_host] fallback: build_and_run_bfree_gui.sh with MVP QML..."
  export BFREE_QML_FILE="$MVP_QML"
  exec bash "$ROOT/build_and_run_bfree_gui.sh"
fi

echo "[demo_mvp_host] install host Qt6 Qml/Quick or add gui_server/integration_gui/run_desktop_shell.sh" >&2
exit 1
