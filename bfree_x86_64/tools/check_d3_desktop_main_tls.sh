#!/usr/bin/env bash
# Reject desktop.elf whose compat still memset's stale maintainer main_tls VA (0x62c9540).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-$ROOT/userland/desktop_qt/desktop.elf}"

if [[ ! -s "$ELF" ]]; then
  echo "FAIL: missing $ELF" >&2
  exit 1
fi

norm() { printf '0x%x' "0x${1#0x}"; }

holder_nm="$(nm "$ELF" 2>/dev/null | awk '/resourceGlobalData/ && /instanceEvE6holder$/ && !/_ZGV/ { print $1; exit }')"
tls_nm="$(nm "$ELF" 2>/dev/null | awk '/[[:space:]]main_tls$/ { print $1; exit }')"
desk_sha="$(sha256sum "$ELF" | awk '{print $1}')"

if [[ "$desk_sha" == "8da145f187b7d47fa08de86fc12b3c970ba45db9e72a5f970bda7f5081d0e0ff" ]]; then
  echo "FAIL: stale desktop.elf sha256 (guest_link_compat not rebuilt)" >&2
  echo "  bash tools/build_desktop_d3_wayland.sh" >&2
  exit 1
fi

if [[ -z "$tls_nm" ]]; then
  echo "FAIL: nm missing main_tls in $ELF" >&2
  exit 1
fi

tls_n="$(norm "0x$tls_nm")"
tls_hex="${tls_n#0x}"

if ! strings "$ELF" 2>/dev/null | grep -qF 'compat build=main_tls-va-v2'; then
  echo "FAIL: $ELF lacks compat build=main_tls-va-v2 (guest_link_compat.o stale?)" >&2
  exit 1
fi

disasm="$(objdump -d "$ELF" 2>/dev/null || true)"
if echo "$disasm" | grep -q '\$0x62c9540'; then
  if [[ "$tls_n" != "0x62c9540" ]] || [[ -n "$holder_nm" && "$(norm "0x$holder_nm")" != "0x62c6160" ]]; then
    echo "FAIL: desktop.elf still embeds maintainer main_tls VA \$0x62c9540 (nm=$tls_n)" >&2
    exit 1
  fi
fi
if ! echo "$disasm" | grep -Eiq "0x${tls_hex}|\\$0x${tls_hex}"; then
  echo "FAIL: desktop.elf disasm lacks main_tls VA immediate $tls_n" >&2
  echo "  bash tools/build_desktop_d3_wayland.sh" >&2
  exit 1
fi

echo "[main_tls-check] OK nm=$tls_n sha256=$desk_sha"
