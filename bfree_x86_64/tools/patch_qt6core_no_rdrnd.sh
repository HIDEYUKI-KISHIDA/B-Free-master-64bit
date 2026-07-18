#!/usr/bin/env bash
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
# Replace RDRAND/RDSEED in libQt6Core qsimd.cpp.o — B-Free guest CPU may #UD on these opcodes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIB="${1:-${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}/lib/libQt6Core.a}"
OBJ="qsimd.cpp.o"
STAMP="${LIB}.no_rdrnd.stamp"

if [[ ! -f "$LIB" ]]; then
  echo "[patch-rdrnd] missing $LIB" >&2
  exit 1
fi

if [[ -f "$STAMP" ]] && [[ "$LIB" -ot "$STAMP" ]]; then
  echo "[patch-rdrnd] already patched: $LIB"
  exit 0
fi

if ! ar t "$LIB" | grep -qx "$OBJ"; then
  echo "[patch-rdrnd] WARN: $OBJ not in $LIB — skip" >&2
  exit 0
fi

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/bfree-qsimd-patch.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT

cd "$WORKDIR"
ar x "$LIB" "$OBJ"
python3 "$ROOT/tools/patch_qsimd_rdrnd.py" "$OBJ"
readelf -h "$OBJ" >/dev/null
cp -a "$LIB" "${LIB}.pre_rdrnd_patch"
ar d "$LIB" "$OBJ"
ar r "$LIB" "$OBJ"
touch "$STAMP"
echo "[patch-rdrnd] OK: $LIB (backup ${LIB}.pre_rdrnd_patch)"
