#!/usr/bin/env bash
# Assemble libstdc++ *-lt.s with x86_64-elf-as and patch Makefiles (libtool CXX + .s -> ld).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LINUX_ROOT="${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}"
BUILD="${BFREE_ELF_GCC_BUILD_DIR:-$LINUX_ROOT/gcc-build}"
export BFREE_BFREE_X86_64_ROOT="$ROOT"
export BFREE_LINUX_BUILD_ROOT="$LINUX_ROOT"
# shellcheck source=bfree_elf_binutils.sh
. "$ROOT/tools/bfree_elf_binutils.sh"

BINUTILS="$(bfree_find_elf_binutils_dir)" || {
  bfree_print_binutils_help
  exit 1
}
AS="$BINUTILS/x86_64-elf-as"
MK_LO="$ROOT/tools/mk_libtool_lo.sh"
run_lf() {
  local src="$1"
  local tmp="/tmp/bfree-$(basename "$src").$$"
  tr -d '\r' <"$src" >"$tmp" && chmod +x "$tmp" && bash "$tmp" && rm -f "$tmp"
}
export BFREE_BFREE_X86_64_ROOT="$ROOT"
export BFREE_LINUX_BUILD_ROOT="$LINUX_ROOT"
chmod +x "$MK_LO" 2>/dev/null || true
run_lf "$ROOT/tools/patch_libstdcxx_makefile_asm.sh"

for sub in c++11 c++98; do
  dir="$BUILD/x86_64-elf/libstdc++-v3/src/$sub"
  [[ -d "$dir" ]] || continue
  shopt -s nullglob
  for asm in "$dir"/*-lt.s; do
    base="$(basename "$asm" -lt.s)"
    o="$dir/${base}.o"
    lo="$dir/${base}.lo"
    echo "[prebuild-asm] $asm -> $o + $lo"
    "$AS" -o "$o" "$asm"
    bash "$MK_LO" "$lo" "$(basename "$o")"
  done
done

echo "[prebuild-asm] done — run: cd $BUILD && make -j\$(nproc) all-target-libstdc++-v3"
