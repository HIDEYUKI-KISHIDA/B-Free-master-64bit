#!/usr/bin/env bash
# Restore known-good desktop.elf from maintainer Program/ tree.
# Use after a partial D3 relink GP (#GP in TLS init, no [desktop_qt] main entry).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
GOOD="${BFREE_DESKTOP_GOOD:-/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/desktop.elf}"

if [[ ! -s "$GOOD" ]]; then
  for alt in "$HOME/bfree_build/userland/desktop_qt/desktop.elf"; do
    if [[ -s "$alt" ]]; then
      GOOD="$alt"
      break
    fi
  done
fi

if [[ ! -s "$GOOD" ]]; then
  echo "FAIL: good desktop.elf not found (set BFREE_DESKTOP_GOOD=)" >&2
  exit 1
fi

sz="$(wc -c < "$GOOD" | tr -d ' ')"
if [[ "$sz" -lt 10000000 ]]; then
  echo "FAIL: $GOOD too small ($sz bytes)" >&2
  exit 1
fi

cp -f "$GOOD" "$DESK/desktop.elf"
bash "$ROOT/tools/update_guest_resource_holder_va.sh" "$DESK/desktop.elf" "$DESK/guest_resource_holder_va.h"
holder="$(sed -n 's/.*HOLDER_VA \([0-9a-fxA-FX]*\)u.*/\1/p' "$DESK/guest_resource_holder_va.h")"
echo "[restore] desktop.elf <= $GOOD ($sz bytes) holder=$holder"
echo "Next: BFREE_D3=1 bash tools/_d3_compositor_stub_smoke.sh"
