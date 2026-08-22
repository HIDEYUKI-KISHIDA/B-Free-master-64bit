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

tls_nm="$(nm "$ELF" 2>/dev/null | awk '/[[:space:]]main_tls$/ { print $1; exit }')"
if [[ -n "$tls_nm" ]]; then
  tls_n="$(norm "0x$tls_nm")"
  tls_region="$(objdump -d "$ELF" 2>/dev/null | awk '
    /<bfree_guest_init_musl_tls>:/ { fn=1; next }
    fn && /^[0-9a-f]+ <[^>]+>:/ { exit }
    fn { print }
  ')"
  if echo "$tls_region" | grep -q '0x62c9540'; then
    echo "FAIL: bfree_guest_init_musl_tls still uses stale maintainer VA 0x62c9540 (nm=$tls_n)" >&2
    echo "  rm userland/desktop_qt/guest_link_compat.o && bash tools/build_desktop_d3_wayland.sh" >&2
    exit 1
  fi
  if ! echo "$tls_region" | grep -q 'main_tls'; then
    echo "FAIL: bfree_guest_init_musl_tls does not reference linker symbol main_tls" >&2
    echo "  bash tools/build_desktop_d3_wayland.sh" >&2
    exit 1
  fi
  echo "[holder-check] main_tls nm=$tls_n (linker symbol ok)"
elif strings "$ELF" 2>/dev/null | grep -qF '[desktop_qt] D3 wayland desk session'; then
  echo "FAIL: main_tls symbol missing from D3 wayland desktop.elf" >&2
  exit 1
fi

if ! strings "$ELF" 2>/dev/null | grep -qF '[desktop_qt] main entry'; then
  echo "WARN: $ELF lacks [desktop_qt] main entry string" >&2
fi

echo "[holder-check] OK"

# Optional: compare against maintainer fingerprint when present.
# D3 wayland relinks produce a different desktop.elf (stub QPA, ~58MB) than the
# maintainer mmap96 good copy (~75MB) — holder VA match above is the real gate.
GOOD_SHA="$ROOT/tools/desktop.elf.good.sha256"
skip_good_sha=0
if [[ "${BFREE_D3_WAYLAND_LINK:-0}" == "1" || "${BFREE_SKIP_DESKTOP_GOOD_SHA:-0}" == "1" ]]; then
  skip_good_sha=1
elif strings "$ELF" 2>/dev/null | grep -qF '[desktop_qt] D3 wayland desk session'; then
  skip_good_sha=1
fi
if [[ -f "$GOOD_SHA" && "$skip_good_sha" != "1" ]]; then
  expect_list="$(grep -E '^[0-9a-f]{64}$' "$GOOD_SHA" || true)"
  if [[ -n "$expect_list" ]]; then
    actual="$(sha256sum "$ELF" | awk '{print $1}')"
    if echo "$expect_list" | grep -qx "$actual"; then
      echo "[holder-check] sha256 OK ($actual)"
    else
      expect="$(echo "$expect_list" | head -1)"
      echo "FAIL: sha256 mismatch (wrong desktop.elf — not maintainer good copy)" >&2
      echo "  actual  $actual" >&2
      echo "  expect  $expect (see $GOOD_SHA for phdr-patched hash)" >&2
      echo "  bash tools/restore_desktop_good_for_d3.sh && bash tools/relink_desktop_phdrs_only.sh" >&2
      exit 1
    fi
  fi
elif [[ "$skip_good_sha" == "1" ]]; then
  actual="$(sha256sum "$ELF" | awk '{print $1}')"
  echo "[holder-check] sha256 skip (D3 wayland relink) actual=$actual"
fi
