#!/usr/bin/env bash
# Vendor wayland.xml for coverage tooling (tools/wayland_core_bfree_coverage.py).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/wayland-upstream.xml"
URL="${BFREE_WAYLAND_XML_URL:-https://gitlab.freedesktop.org/wayland/wayland/-/raw/main/protocol/wayland.xml}"
if [[ -f "$OUT" && "${BFREE_WAYLAND_XML_FORCE:-0}" != "1" ]]; then
  echo "[vendor_wayland] already present: $OUT"
  exit 0
fi
echo "[vendor_wayland] fetching $URL"
curl -fsSL "$URL" -o "$OUT"
echo "[vendor_wayland] wrote $OUT ($(wc -c <"$OUT") bytes)"
