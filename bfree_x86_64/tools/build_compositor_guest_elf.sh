#!/usr/bin/env bash
# Cross-build gui_server → compositor.elf for ISO / BFREE_BOOT_GUI_FIRST boot.
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GS="$ROOT/gui_server"

if [[ ! -d "$GS" ]]; then
  echo "[compositor-guest] missing $GS" >&2
  exit 1
fi

export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"
BFREE_ROOT="$ROOT" bash <(sed 's/\r$//' "$ROOT/tools/ensure_x86_64_elf_toolchain.sh")

is_guest_elf() {
  local f="$1"
  [[ -f "$f" ]] || return 1
  file "$f" 2>/dev/null | grep -q 'ELF 64-bit' || return 1
  file "$f" 2>/dev/null | grep -qi 'x86-64' || return 1
  ! file "$f" 2>/dev/null | grep -qE 'dynamically linked|GNU/Linux|interpreter'
}

pick_existing() {
  local c
  for c in \
    "$GS/compositor.elf" \
    "$GS/build/compositor.elf" \
    "$GS/compositor/compositor.elf"; do
    if is_guest_elf "$c"; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

if existing="$(pick_existing)"; then
  echo "[compositor-guest] reusing guest ELF: $existing"
  exit 0
fi

# Prefer explicit guest target in local Makefile when present.
if [[ -f "$GS/Makefile" ]] && make -C "$GS" -n compositor.elf >/dev/null 2>&1; then
  echo "[compositor-guest] make -C gui_server compositor.elf"
  make -C "$GS" compositor.elf
  if existing="$(pick_existing)"; then
    echo "[compositor-guest] built via gui_server/Makefile: $existing"
    exit 0
  fi
fi

for _musl in "${BFREE_ELF_LIBM_DIR:-}" "$ROOT/out/x86_64-elf-libm" "/root/out/x86_64-elf-libm"; do
  [[ -n "$_musl" && -f "$_musl/libc.a" ]] || continue
  export BFREE_ELF_LIBM_DIR="$_musl"
  break
done

_resolve() {
  bash -c 'tr -d "\r" < "$1" | bash -s -- "$2" "$3"' _ \
    "$ROOT/tools/resolve_elf_runtime_libs.sh" \
    "${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}" \
    "${BFREE_ELF_CXX:-$(command -v x86_64-elf-g++)}"
}

if ! _resolve >/dev/null 2>&1; then
  echo "[compositor-guest] musl libc missing — building..."
  BFREE_ELF_LIBM_DIR="${BFREE_ELF_LIBM_DIR:-$ROOT/out/x86_64-elf-libm}" \
    bash -c 'tr -d "\r" < "$1" | bash -s' _ "$ROOT/tools/build_x86_64_elf_libm.sh"
fi

read -r CRT0 LIBC LIBM LIBGCC _rest <<<"$(_resolve)"
export BFREE_ELF_CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}"
export CRT0 LIBC LIBM LIBGCC BFREE_ELF_LIBM_DIR

echo "[compositor-guest] cross-link via tools/compositor_guest.mk"
make -f "$ROOT/tools/compositor_guest.mk" clean 2>/dev/null || true
make -f "$ROOT/tools/compositor_guest.mk" \
  CC="$BFREE_ELF_CC" \
  CRT0="$CRT0" LIBC="$LIBC" LIBM="$LIBM" LIBGCC="$LIBGCC"

if existing="$(pick_existing)"; then
  echo "[compositor-guest] OK: $existing ($(stat -c%s "$existing" 2>/dev/null || echo '?') bytes)"
  exit 0
fi

if [[ -f "$GS/tron_gui_server" ]]; then
  echo "[compositor-guest] ERROR: only host binary tron_gui_server was built (Linux ELF)." >&2
  echo "  Host gcc target cannot boot as compositor.elf on B-Free." >&2
  echo "  Fix: ensure gui_server/*.c compile with x86_64-elf-gcc + musl (this script)." >&2
  file "$GS/tron_gui_server" >&2 || true
fi
echo "[compositor-guest] compositor.elf not produced" >&2
exit 1
