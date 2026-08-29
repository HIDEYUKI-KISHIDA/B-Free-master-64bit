#!/usr/bin/env bash
# Bootstrap or refresh guest_resource_holder_va.h (never copy stale maintainer header).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$ROOT/userland/desktop_qt/guest_resource_holder_va.h}"
elf="${2:-$ROOT/userland/desktop_qt/desktop.elf}"

if [[ -s "$elf" ]] && bash "$ROOT/tools/update_guest_resource_holder_va.sh" "$elf" "$out" 2>/dev/null; then
  exit 0
fi

if [[ -f "$out" ]] && grep -q 'BFREE_DESKTOP_MAIN_TLS_VA' "$out" && grep -q 'BFREE_GUEST_QT_RESOURCE_HOLDER_VA' "$out"; then
  exit 0
fi

cat >"$out" <<'EOF'
/* Bootstrap — tools/update_guest_resource_holder_va.sh after desktop.elf link. */
#define BFREE_GUEST_QT_RESOURCE_HOLDER_VA 0u
#define BFREE_DESKTOP_MAIN_TLS_VA 0u
#define BFREE_DESKTOP_MAIN_TLS_BYTES 48u
EOF
echo "[ensure_guest_resource_holder_va] bootstrap -> $out"
