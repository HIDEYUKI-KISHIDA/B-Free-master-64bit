#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

RUNS="${RUNS:-5}"
RUNTIME_SEC="${RUNTIME_SEC:-12}"
# Default: animated QML so commit_rate / approx_fps in the report are non-zero (Option A).
# Override e.g. CLIENT_CMD="wayland-info" for connect-only smoke.
CLIENT_CMD="${CLIENT_CMD:-QT_QPA_PLATFORM=wayland qmlscene tools/qml_wayland_kpi_bench.qml}"
LOG_FILE="${LOG_FILE:-$ROOT_DIR/logs/wayland_server.log}"
OUT_CSV="${OUT_CSV:-$ROOT_DIR/logs/wayland_kpi_report.csv}"
OUT_MD="${OUT_MD:-$ROOT_DIR/logs/wayland_kpi_report.md}"
RUN_LOG_DIR="${RUN_LOG_DIR:-$ROOT_DIR/logs/wayland_kpi_runs}"

mkdir -p "$ROOT_DIR/logs"
mkdir -p "$RUN_LOG_DIR"

echo "[kpi] build gui_server"
make -C gui_server >/dev/null

echo "run,rc,client_rc,interop_ok,req,unknown,commits,frame_done,pointer_ev,key_ev,touch_ev,session_ms,approx_fps,commit_rate,run_log" > "$OUT_CSV"

for i in $(seq 1 "$RUNS"); do
  echo "[kpi] run $i/$RUNS client=$CLIENT_CMD runtime=${RUNTIME_SEC}s"
  run_log="$RUN_LOG_DIR/run_${i}.log"
  set +e
  CLIENT_CMD="$CLIENT_CMD" RUNTIME_SEC="$RUNTIME_SEC" \
    bash gui_server/integration_gui/run_wayland_real_compat_with_client.sh >"$run_log" 2>&1
  rc=$?
  set -e

  python3 - "$LOG_FILE" "$OUT_CSV" "$i" "$rc" "$run_log" <<'PY'
import re
import sys

log_path, out_csv, run_idx, outer_rc, run_log = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
kpi_pat = re.compile(
    r"\[wl_kpi\]\s+req=(\d+)\s+unknown=(\d+)\s+commits=(\d+)\s+frame_done=(\d+)\s+"
    r"pointer_ev=(\d+)\s+key_ev=(\d+)\s+touch_ev=(\d+)\s+session_ms=(\d+)"
)
client_rc_pat = re.compile(r"\[real-compat-client\]\s+client rc=(\d+)")
req = unknown = commits = frame_done = pointer_ev = key_ev = touch_ev = session_ms = 0
client_rc = outer_rc
interop_ok = 0
try:
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    matches = kpi_pat.findall(text)
    if matches:
        req, unknown, commits, frame_done, pointer_ev, key_ev, touch_ev, session_ms = map(int, matches[-1])
except FileNotFoundError:
    pass

try:
    with open(run_log, "r", encoding="utf-8", errors="replace") as f:
        run_text = f.read()
    m = client_rc_pat.findall(run_text)
    if m:
        client_rc = int(m[-1])
    if "interop check: PASSED" in run_text:
        interop_ok = 1
except FileNotFoundError:
    pass

approx_fps = 0.0
commit_rate = 0.0
if session_ms > 0:
    approx_fps = (frame_done * 1000.0) / session_ms
    commit_rate = (commits * 1000.0) / session_ms

with open(out_csv, "a", encoding="utf-8") as f:
    f.write(
        f"{run_idx},{outer_rc},{client_rc},{interop_ok},{req},{unknown},{commits},{frame_done},{pointer_ev},{key_ev},{touch_ev},{session_ms},{approx_fps:.3f},{commit_rate:.3f},{run_log}\n"
    )
PY
done

python3 - "$OUT_CSV" "$OUT_MD" "$RUNS" "$CLIENT_CMD" "$RUNTIME_SEC" <<'PY'
import csv
import sys
from statistics import mean

csv_path, md_path, runs, client_cmd, runtime_sec = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], int(sys.argv[5])
rows = []
with open(csv_path, "r", encoding="utf-8") as f:
    rows = list(csv.DictReader(f))

if not rows:
    raise SystemExit("No KPI rows generated")

def as_int(k):
    return [int(r[k]) for r in rows]

def as_float(k):
    return [float(r[k]) for r in rows]

rcs = as_int("client_rc")
fps = as_float("approx_fps")
commit_rate = as_float("commit_rate")
latency_ms = as_int("session_ms")
unknown = as_int("unknown")
failures = sum(1 for v in rcs if v not in (0, 124))
failure_rate = (failures * 100.0) / len(rows)
interop_pass = sum(int(r["interop_ok"]) for r in rows)

with open(md_path, "w", encoding="utf-8") as f:
    f.write("# Wayland KPI Report\n\n")
    f.write(f"- runs: {runs}\n")
    f.write(f"- client: `{client_cmd}`\n")
    f.write(f"- runtime_sec: {runtime_sec}\n\n")
    f.write("## Aggregate\n\n")
    f.write(f"- failure_rate(non-zero/timeout以外): {failure_rate:.2f}% ({failures}/{len(rows)})\n")
    f.write(f"- avg_session_ms: {mean(latency_ms):.1f}\n")
    f.write(f"- p95_session_ms: {sorted(latency_ms)[int(0.95*(len(rows)-1))]}\n")
    f.write(f"- avg_approx_fps: {mean(fps):.3f}\n")
    f.write(f"- avg_commit_rate(commits/sec): {mean(commit_rate):.3f}\n")
    f.write(f"- max_unknown_req: {max(unknown)}\n\n")
    f.write(f"- interop_pass_runs: {interop_pass}/{len(rows)}\n\n")
    if failures > 0:
        reason_counts = {}
        def detect_reason(path):
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as rf:
                    t = rf.read()
            except FileNotFoundError:
                return "run_log_missing"
            tl = t.lower()
            if "client connected" not in tl:
                return "no_wayland_connection"
            if "qt.qpa.plugin" in tl and "could not load" in tl:
                return "qt_platform_plugin_load_failed"
            if "could not connect to display" in tl:
                return "display_connect_failed"
            if "no such file or directory" in tl and "qml_wayland_smoke.qml" in tl:
                return "qml_file_missing"
            if "null string received on non-nullable type" in tl and "geometry" in tl:
                return "wl_output_geometry_protocol_violation"
            if "file descriptor expected" in tl and "keymap" in tl:
                return "wl_keyboard_keymap_missing_fd"
            if "message too short" in tl and "scale" in tl:
                return "wl_output_scale_event_mismatch"
            if "message too short" in tl and "modifiers" in tl:
                return "wl_keyboard_modifiers_truncated"
            if "wayland connection experienced a fatal error" in tl:
                return "wayland_wire_protocol_error"
            if "timeout" in tl or "client rc=124" in tl:
                return "client_timeout"
            return "other_client_failure"

        f.write("## Failed Runs (rc)\n\n")
        for r in rows:
            rc = int(r["client_rc"])
            if rc not in (0, 124):
                reason = detect_reason(r["run_log"])
                reason_counts[reason] = reason_counts.get(reason, 0) + 1
                f.write(f"- run {r['run']}: rc={rc}, reason={reason}, log=`{r['run_log']}`\n")
        if reason_counts:
            f.write("\n## Failure Reason Summary\n\n")
            for k in sorted(reason_counts):
                f.write(f"- {k}: {reason_counts[k]}\n")
        f.write("\n")
    f.write("## Raw CSV\n\n")
    f.write(f"- `{csv_path}`\n")
PY

echo "[kpi] wrote: $OUT_CSV"
echo "[kpi] wrote: $OUT_MD"
echo "[kpi] run logs: $RUN_LOG_DIR"
