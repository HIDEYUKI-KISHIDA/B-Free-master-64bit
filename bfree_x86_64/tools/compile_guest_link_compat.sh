#!/usr/bin/env bash
# Compile tools/guest_link_compat.cpp -> guest_link_compat.o (desktop / hello compat).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:?usage: compile_guest_link_compat.sh OUT.o [extra -D flags...]}"
shift

export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"
export BFREE_ROOT="$ROOT"
export HOME="${HOME:-/home/h_kis}"

DESK="$ROOT/userland/desktop_qt"
MUSL_INC=""
for musl in "$ROOT/out/x86_64-elf-libm/prefix/include" \
            /root/out/x86_64-elf-libm/prefix/include \
            "$HOME/out/x86_64-elf-libm/prefix/include"; do
  [[ -f "$musl/stdio.h" ]] && MUSL_INC="$musl" && break
done
[[ -n "$MUSL_INC" ]] || { echo "FAIL: musl headers (out/x86_64-elf-libm/prefix/include)" >&2; exit 1; }

LIBC_HDR="$ROOT/userland/libc/musl_libc_libc.h"
[[ -f "$LIBC_HDR" ]] || { echo "FAIL: missing $LIBC_HDR" >&2; exit 1; }

if [[ ! -f "$DESK/guest_serial.h" ]]; then
  for fb in "/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/guest_serial.h" \
            "$HOME/bfree_build/userland/desktop_qt/guest_serial.h"; do
    if [[ -f "$fb" ]]; then
      cp -f "$fb" "$DESK/guest_serial.h"
      echo "[compat-compile] restored guest_serial.h <= $fb"
      break
    fi
  done
fi
[[ -f "$DESK/guest_serial.h" ]] || {
  echo "FAIL: missing $DESK/guest_serial.h (copy from Program/bfree_x86_64/userland/desktop_qt/)" >&2
  exit 1
}

restore_desk_file() {
  local name="$1"
  [[ -f "$DESK/$name" ]] && return 0
  for fb in "/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/$name" \
            "$HOME/bfree_build/userland/desktop_qt/$name"; do
    if [[ -f "$fb" ]]; then
      cp -f "$fb" "$DESK/$name"
      echo "[compat-compile] restored $name <= $fb"
      return 0
    fi
  done
  return 1
}

if ! restore_desk_file "guest_mvp_shell_qml.inc"; then
  if [[ -f "$DESK/GuestMvpShell_smoke.qml" ]]; then
    python3 "$ROOT/tools/gen_guest_mvp_shell_bytes.py"
  else
    echo "FAIL: missing $DESK/guest_mvp_shell_qml.inc (copy from Program/ or add GuestMvpShell_smoke.qml)" >&2
    exit 1
  fi
fi

if [[ ! -f "$DESK/guest_resource_holder_va.h" && -s "$DESK/desktop.elf" ]]; then
  bash "$ROOT/tools/update_guest_resource_holder_va.sh" "$DESK/desktop.elf" "$DESK/guest_resource_holder_va.h"
fi
bash "$ROOT/tools/ensure_guest_resource_holder_va.sh" "$DESK/guest_resource_holder_va.h" "$DESK/desktop.elf"

EXTRA=("$@")
if ! grep -q 'compat build=main_tls-va-v2 defer-env-v1' "$ROOT/tools/guest_link_compat.cpp"; then
  echo "FAIL: $ROOT/tools/guest_link_compat.cpp lacks defer-env-v1 TLS fix (git pull blocked?)" >&2
  echo "  bash tools/wsl_sync_d3_branch.sh" >&2
  exit 1
fi
if ! grep -q 'bfree_guest_fill_auxv_core' "$ROOT/tools/guest_link_compat.cpp"; then
  echo "FAIL: $ROOT/tools/guest_link_compat.cpp missing bfree_guest_fill_auxv_core" >&2
  echo "  bash tools/wsl_sync_d3_branch.sh" >&2
  exit 1
fi
if ! grep -q 'BFREE_DESKTOP_MAIN_TLS_VA' "$DESK/guest_resource_holder_va.h"; then
  echo "FAIL: $DESK/guest_resource_holder_va.h lacks BFREE_DESKTOP_MAIN_TLS_VA" >&2
  exit 1
fi
x86_64-elf-g++ -m64 -mcmodel=large -mno-red-zone -fno-stack-protector -fno-stack-check \
  -fno-stack-clash-protection -fno-pic \
  -Wall -Wextra -mno-sse -mno-mmx -mno-3dnow -fno-exceptions -fno-rtti -Wa,--noexecstack \
  -isystem "$MUSL_INC" -D_GNU_SOURCE -D__linux__ \
  "${EXTRA[@]}" \
  -x c++ -c -o "$OUT" "$ROOT/tools/guest_link_compat.cpp"

if ! strings "$OUT" 2>/dev/null | grep -qF 'compat build=main_tls-va-v2 defer-env-v1'; then
  echo "FAIL: $OUT lacks compat build id defer-env-v1 (stale guest_link_compat.cpp?)" >&2
  exit 1
fi
hdr_tls="$(sed -n 's/.*BFREE_DESKTOP_MAIN_TLS_VA \([0-9a-fxA-FX]*\)u.*/\1/p' "$DESK/guest_resource_holder_va.h" | head -1)"
if [[ -n "$hdr_tls" && "$hdr_tls" != "0" && "$hdr_tls" != "0x0" ]]; then
  tls_hex="$(printf '%x' "$hdr_tls")"
  if objdump -d "$OUT" 2>/dev/null | grep -q '\$0x62c9540'; then
    echo "FAIL: $OUT still uses hardcoded maintainer VA \$0x62c9540 (header=$hdr_tls)" >&2
    exit 1
  fi
  if ! objdump -d "$OUT" 2>/dev/null | grep -Eiq "0x${tls_hex}|\$0x${tls_hex}"; then
    echo "WARN: $OUT disasm missing header main_tls VA $hdr_tls (first link pass?)" >&2
  fi
fi
echo "[compat-compile] OK defer-env-v1 $(stat -c%s "$OUT") bytes hdr_tls=${hdr_tls:-0}"
