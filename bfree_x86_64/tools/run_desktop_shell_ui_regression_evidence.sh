#!/usr/bin/env bash
# DesktopShell UI 要件（v2.0.1）実機回帰証跡の統合収録。
# 生成物: Program/bfree_x86_64/logs/desktop_shell_ui_regression_evidence.md
# 環境変数: UI_EVIDENCE_KPI_RUNS（既定 3）, UI_EVIDENCE_KPI_SEC（既定 10）, SKIP_BUILD（子に伝搬可）
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LOGDIR="$ROOT_DIR/logs"
OUT="$LOGDIR/desktop_shell_ui_regression_evidence.md"
mkdir -p "$LOGDIR"

GIT_REV="${GIT_REV:-}"
if [[ -z "$GIT_REV" ]] && command -v git >/dev/null 2>&1; then
  GIT_REV="$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"
fi
: "${GIT_REV:=unknown}"

KPI_RUNS="${UI_EVIDENCE_KPI_RUNS:-3}"
KPI_SEC="${UI_EVIDENCE_KPI_SEC:-10}"

append_block() {
  local title="$1"
  shift
  {
    echo ""
    echo "### $title"
    echo ""
    echo "--- log begin ---"
    set +e
    "$@"
    local rc=$?
    echo "--- log end (exit=$rc) ---"
    echo ""
  } >>"$OUT"
}

{
  echo "# DesktopShell UI / Wayland 実機回帰証跡（自動収録）"
  echo ""
  echo "- 収録日時: $(date -Iseconds 2>/dev/null || date)"
  echo "- リポジトリ根: ${ROOT_DIR}"
  echo "- git rev: ${GIT_REV}"
  echo "- KPI パラメータ: RUNS=${KPI_RUNS} RUNTIME_SEC=${KPI_SEC}"
  echo ""
  echo "## 1. 本スクリプトが順に実行する検証"
  echo ""
  echo "1. tools/run_wayland_full_regression.sh … Wayland ビルド・compat・coverage"
  echo "2. tools/run_wayland_kpi_report.sh … Qt+qmlscene KPI（既定ベンチ）"
  echo "3. gui_server/integration_gui/run_wayland_interop_matrix.sh … interop マトリクス"
  echo ""
  echo "## 2. 各ステップのコンソール出力"
  echo ""
} >"$OUT"

append_block "Step A: run_wayland_full_regression.sh" \
  bash "$ROOT_DIR/tools/run_wayland_full_regression.sh"

append_block "Step B: run_wayland_kpi_report.sh" \
  env RUNS="$KPI_RUNS" RUNTIME_SEC="$KPI_SEC" bash "$ROOT_DIR/tools/run_wayland_kpi_report.sh"

append_block "Step C: run_wayland_interop_matrix.sh" \
  bash "$ROOT_DIR/gui_server/integration_gui/run_wayland_interop_matrix.sh"

{
  echo "## 3. 参照すべき生成ログ（パス）"
  echo ""
  echo "- ${LOGDIR}/wayland_server.log（存在すれば）"
  echo "- ${LOGDIR}/wayland_kpi_report.md / wayland_kpi_report.csv"
  echo "- ${LOGDIR}/wayland_kpi_runs/（KPI 各ラン）"
  echo "- tools/wayland_core 由来の coverage 出力（full_regression 内で更新される場合）"
  echo ""
  echo "## 4. 仕様書との対応"
  echo ""
  echo "TK2_DesktopShell_UI_UX_Spec_v2.0.1.md の「付録 B」で本ファイルを自動証跡の正とする。"
  echo "手動 UI のメモを同一 logs/ 配下に置く場合はファイル名を揃えず任意でよいが、実行日と操作概要を併記すること。"
  echo ""
} >>"$OUT"

echo "[desktop_shell_ui_evidence] wrote: $OUT"
