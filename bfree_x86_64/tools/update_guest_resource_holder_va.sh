#!/usr/bin/env bash
# Emit layout-dependent desktop BSS VAs from linked desktop.elf (holder + musl main_tls).
set -eu
elf="${1:?usage: update_guest_resource_holder_va.sh desktop.elf [out.h]}"
out="${2:-$(dirname "$elf")/guest_resource_holder_va.h}"
hva="$(nm "$elf" 2>/dev/null | awk '/resourceGlobalData/ && /instanceEvE6holder$/ && !/_ZGV/ { print "0x" $1; exit }')"
tls_va="$(nm "$elf" 2>/dev/null | awk '$NF == "main_tls" { print "0x" $1; exit }')"
if [ -z "$hva" ]; then
  echo "[update_guest_resource_holder_va] holder symbol not found in $elf" >&2
  exit 1
fi
if [ -z "$tls_va" ]; then
  echo "[update_guest_resource_holder_va] main_tls symbol not found in $elf" >&2
  exit 1
fi
tmp="${out}.tmp"
{
  printf '#define BFREE_GUEST_QT_RESOURCE_HOLDER_VA %su\n' "$hva"
  printf '#define BFREE_DESKTOP_MAIN_TLS_VA %su\n' "$tls_va"
  printf '#define BFREE_DESKTOP_MAIN_TLS_BYTES 48u\n'
} >"$tmp"
if [ -f "$out" ] && cmp -s "$out" "$tmp"; then
  rm -f "$tmp"
else
  mv -f "$tmp" "$out"
  compat_o="$(dirname "$out")/guest_link_compat.o"
  rm -f "$compat_o"
  echo "[update_guest_resource_holder_va] holder=$hva main_tls=$tls_va -> $out (invalidated $compat_o)"
fi
