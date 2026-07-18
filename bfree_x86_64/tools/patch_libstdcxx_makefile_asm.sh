#!/usr/bin/env bash
# Rewrite *-lt.s libtool rules to use x86_64-elf-as (libtool CXX + .s invokes ld on musl cross).
set -euo pipefail

ROOT="${BFREE_BFREE_X86_64_ROOT:?set BFREE_BFREE_X86_64_ROOT}"
LINUX_ROOT="${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}"
BUILD="${BFREE_ELF_GCC_BUILD_DIR:-$LINUX_ROOT/gcc-build}"
# shellcheck source=bfree_elf_binutils.sh
. "$ROOT/tools/bfree_elf_binutils.sh"
BINUTILS="$(bfree_find_elf_binutils_dir)" || { bfree_print_binutils_help; exit 1; }
AS="$BINUTILS/x86_64-elf-as"
MK_LO="$ROOT/tools/mk_libtool_lo.sh"

patch_makefile() {
  local mf="$1"
  [[ -f "$mf" ]] || return 0
  local tmp="${mf}.bfree_asm"
  : >"$tmp"
  local line lt_rule=0 lt_s_rule=0
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ "$line" =~ ^([A-Za-z0-9_.+-]+)\.lo:[[:space:]]+([A-Za-z0-9_.+-]+)-lt\.s ]]; then
      local tgt="${BASH_REMATCH[1]}.lo"
      local src="${BASH_REMATCH[2]}-lt.s"
      local base="${BASH_REMATCH[1]}"
      printf '%s: %s\n' "$tgt" "$src" >>"$tmp"
      printf '\t@test -f %s.o || %s -o %s.o %s\n' "$base" "$AS" "$base" "$src" >>"$tmp"
      printf '\t@test -f %s || %s %s %s.o\n' "$tgt" "$MK_LO" "$tgt" "$base" >>"$tmp"
      lt_rule=1
      lt_s_rule=0
      continue
    fi
    if [[ "$line" =~ ^([A-Za-z0-9_.+-]+)-lt\.s:[[:space:]]+([A-Za-z0-9_.+-]+\.(cc|cpp|cxx)) ]]; then
      printf '%s\n' "$line" >>"$tmp"
      lt_s_rule=1
      lt_rule=0
      continue
    fi
    if (( lt_rule )); then
      [[ "$line" =~ ^[[:space:]] ]] && continue
      lt_rule=0
    fi
    if (( lt_s_rule )); then
      if [[ "$line" =~ ^[[:space:]] ]]; then
        if [[ "$line" == *'$(LTCXXCOMPILE)'* && "$line" == *'-S'* ]]; then
          line="${line//\$(LTCXXCOMPILE)/\$(CXXCOMPILE)}"
        fi
        if [[ "$line" == *tmp-*-lt.o* && "$line" == *mv* ]]; then
          continue
        fi
        printf '%s\n' "$line" >>"$tmp"
        continue
      fi
      lt_s_rule=0
    fi
    printf '%s\n' "$line" >>"$tmp"
  done <"$mf"
  mv "$tmp" "$mf"
  echo "[patch-asm] $mf"
}

clean_stale_elf_lt_s() {
  local d="$1"
  [[ -d "$d" ]] || return 0
  local f base
  for f in "$d"/*-lt.s; do
    [[ -f "$f" ]] || continue
    if file -b "$f" | grep -q '^ELF'; then
      base="${f%-lt.s}"
      echo "[patch-asm] remove stale ELF disguised as asm: $f"
      rm -f "$f" "${base}.o" "${base}.lo" "$d/tmp$(basename "$f")"
    fi
  done
}

for sub in c++11 c++98; do
  d="$BUILD/x86_64-elf/libstdc++-v3/src/$sub"
  clean_stale_elf_lt_s "$d"
  patch_makefile "$d/Makefile"
done
