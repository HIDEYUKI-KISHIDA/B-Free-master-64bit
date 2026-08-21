#!/usr/bin/env bash
# Verify BFREE_GUEST_QT_RESOURCE_HOLDER_VA baked into desktop.elf matches nm symbol.
# Catches partial relinks that update BSS but leave stale immediates in guest_link_compat.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-$ROOT/userland/desktop_qt/desktop.elf}"

if [[ ! -s "$ELF" ]]; then
  echo "FAIL: missing $ELF" >&2
  exit 1
fi

holder_nm="$(nm "$ELF" 2>/dev/null | awk '/resourceGlobalData/ && /instanceEvE6holder$/ && !/_ZGV/ { print $1; exit }')"
if [[ -z "$holder_nm" ]]; then
  echo "FAIL: resourceGlobalData holder symbol not found in $ELF" >&2
  exit 1
fi

# movq $IMM, -0x8(%rbp) in bfree_guest_resource_pin_global_guard
embed="$(objdump -d "$ELF" 2>/dev/null | awk '
  /bfree_guest_resource_pin_global_guard/ { fn=1; next }
  fn && /movq.*-0x8\(%rbp\)/ {
    if (match($0, /\$0x[0-9a-f]+/)) { print substr($0, RSTART+1, RLENGTH-1); exit }
  }
')"
if [[ -z "$embed" ]]; then
  # Fallback: any mov $holder,%edi in pin_global_guard region
  embed="$(objdump -d "$ELF" 2>/dev/null | awk -v stop="bfree_guest_resource_list_sanitize" '
    /bfree_guest_resource_pin_global_guard/ { fn=1; next }
    fn && $0 ~ stop { exit }
    fn && /mov.*\$0x[0-9a-f]+/ {
      if (match($0, /\$0x[0-9a-f]+/)) { print substr($0, RSTART+1, RLENGTH-1); exit }
    }
  ')"
fi

nm_norm="0x$(printf '%x' "0x$holder_nm")"
# Normalize hex (strip leading zeros for compare)
norm() { printf '0x%x' "0x${1#0x}"; }
nm_n="$(norm "$nm_norm")"
em_n="$(norm "$embed")"

echo "[holder-check] nm=$nm_n embed=$em_n elf=$ELF"
if [[ "$nm_n" != "$em_n" ]]; then
  echo "FAIL: embedded holder VA != nm (GP on vfork exec — partial relink)" >&2
  echo "  bash tools/restore_desktop_good_for_d3.sh" >&2
  exit 1
fi

if ! strings "$ELF" 2>/dev/null | grep -qF '[desktop_qt] main entry'; then
  echo "WARN: $ELF lacks [desktop_qt] main entry string" >&2
fi

echo "[holder-check] OK"
