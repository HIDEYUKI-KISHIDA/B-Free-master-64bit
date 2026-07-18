#!/usr/bin/env bash
# Print libstdc++ include path for x86_64-elf-g++ guest link (guest_link_compat needs <chrono>).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="${HOME}/bin:/root/x86_64-elf-toolchain/bin:/usr/local/x86_64-elf/bin:${PATH}"
GXX=$(command -v x86_64-elf-g++ || true)
if [[ -z "$GXX" ]]; then
  echo "x86_64-elf-g++ not in PATH" >&2
  exit 1
fi
echo "g++: $GXX"
echo "--- libstdc++.a (resolve_elf_runtime_libs) ---"
CC=$(command -v x86_64-elf-gcc 2>/dev/null || true)
bash "$ROOT/tools/resolve_elf_runtime_libs.sh" "$CC" "$GXX" 2>/dev/null | tr ' ' '\n' | grep -E 'libstdc\+\+\.a$' || true
echo "--- recommended include (chrono) ---"
if inc="$(BFREE_ROOT="$ROOT" bash "$ROOT/tools/resolve_elf_cxx_include.sh")"; then
  echo "$inc"
  echo ""
  echo "export BFREE_ELF_CXX_INCLUDE=$inc"
  test -f "$inc/chrono" && echo "[ok] $inc/chrono"
  for sub in x86_64-pc-elf x86_64-elf; do
    if [[ -f "$inc/$sub/bits/c++config.h" ]]; then
      echo "[ok] $inc/$sub/bits/c++config.h (qmake needs this -isystem)"
    fi
  done
  musl="${BFREE_ELF_LIBM_DIR:-$ROOT/out/x86_64-elf-libm}/prefix/include"
  if [[ -f "$musl/stdint.h" ]]; then
    echo "[ok] $musl/stdint.h (qmake -idirafter for libstdc++ #include_next)"
  [[ -f "$musl/math.h" ]] && echo "[ok] $musl/math.h"
  else
    echo "[WARN] $musl/stdint.h missing — bash tools/build_x86_64_elf_libm.sh" >&2
  fi
else
  exit 1
fi
