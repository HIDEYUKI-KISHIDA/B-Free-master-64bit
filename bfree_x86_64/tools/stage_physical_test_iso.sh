#!/usr/bin/env bash
# Copy bfree.iso to a distinct filename for physical USB/CD test, with a small manifest.
# Reversible: does not modify source; keeps original bfree.iso; you can archive outputs.
#
# Usage (from Program/bfree_x86_64):
#   bash tools/stage_physical_test_iso.sh
#
# Environment:
#   BFREE_ISO_SRC        — default: $ROOT/bfree.iso
#   BFREE_PHYSICAL_ISO_OUT — full path of output .iso (optional; default under dist/)
#   BFREE_PHYSICAL_DIST  — output directory when BFREE_PHYSICAL_ISO_OUT unset (default: dist)

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SRC="${BFREE_ISO_SRC:-$ROOT/bfree.iso}"
DIST="${BFREE_PHYSICAL_DIST:-$ROOT/dist}"
mkdir -p "$DIST"

if [[ ! -f "$SRC" ]]; then
  echo "[stage_physical_test_iso] ERROR: missing ISO: $SRC" >&2
  echo "  Build first: bash build.sh   (from $ROOT)" >&2
  exit 1
fi

TS="$(date -u +%Y%m%d-%H%M%S 2>/dev/null || date +%Y%m%d-%H%M%S)"
GITREV="unknown"
if command -v git >/dev/null 2>&1 && git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  GITREV="$(git -C "$ROOT" describe --always --dirty 2>/dev/null || git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
fi

if [[ -n "${BFREE_PHYSICAL_ISO_OUT:-}" ]]; then
  OUT="$BFREE_PHYSICAL_ISO_OUT"
else
  OUT="$DIST/bfree-physical-test-${TS}-${GITREV}.iso"
fi

MANIFEST="${OUT%.iso}.manifest.txt"
cp -f -- "$SRC" "$OUT"
SIZE="$(wc -c < "$OUT" | tr -d ' ')"

{
  echo "bfree physical-test ISO staging"
  echo "generated_utc: ${TS}"
  echo "source_iso: $SRC"
  echo "output_iso: $OUT"
  echo "bytes: $SIZE"
  echo "git_rev: $GITREV"
  echo "host: $(uname -a 2>/dev/null || echo n/a)"
} > "$MANIFEST"

echo "[stage_physical_test_iso] wrote:"
echo "  $OUT"
echo "  $MANIFEST"
