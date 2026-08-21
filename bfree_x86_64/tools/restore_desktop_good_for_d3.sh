#!/usr/bin/env bash
# Restore known-good desktop.elf from maintainer tree (fixes GP after partial D3 relink).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
CHECK="$ROOT/tools/check_desktop_holder_embedded.sh"

try_sources() {
  [[ -n "${BFREE_DESKTOP_GOOD:-}" && -s "$BFREE_DESKTOP_GOOD" ]] && echo "$BFREE_DESKTOP_GOOD"
  echo "/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/desktop.elf"
  echo "$HOME/out/bfree-good-20260816/desktop.elf"
  echo "$HOME/bfree_build/userland/desktop_qt/desktop.elf"
}

restore_one() {
  local src="$1"
  [[ ! -s "$src" ]] && return 1
  local sz
  sz="$(wc -c < "$src" | tr -d ' ')"
  [[ "$sz" -lt 10000000 ]] && return 1
  echo "[restore] trying $src ($sz bytes)"
  cp -f "$src" "$DESK/desktop.elf"
  if bash "$CHECK" "$DESK/desktop.elf" >/dev/null 2>&1; then
    bash "$ROOT/tools/update_guest_resource_holder_va.sh" "$DESK/desktop.elf" "$DESK/guest_resource_holder_va.h"
    holder="$(sed -n 's/.*HOLDER_VA \([0-9a-fxA-FX]*\)u.*/\1/p' "$DESK/guest_resource_holder_va.h")"
    echo "[restore] OK <= $src holder=$holder"
    bash "$CHECK" "$DESK/desktop.elf"
    return 0
  fi
  echo "[restore] skip $src (embedded holder mismatch)" >&2
  return 1
}

if [[ -n "${BFREE_DESKTOP_GOOD:-}" ]]; then
  restore_one "$BFREE_DESKTOP_GOOD" && exit 0
fi

while IFS= read -r src; do
  [[ -z "$src" ]] && continue
  restore_one "$src" && exit 0
done < <(try_sources | awk '!seen[$0]++')

echo "FAIL: no good desktop.elf found" >&2
echo "  Set BFREE_DESKTOP_GOOD=/path/to/desktop.elf (nm == embedded holder)" >&2
echo "  Or copy a verified desktop from Program/ maintainer tree" >&2
exit 1
