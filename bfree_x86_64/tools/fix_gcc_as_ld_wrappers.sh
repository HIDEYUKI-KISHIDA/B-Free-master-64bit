#!/usr/bin/env bash
# gcc-build/gcc/{as,ld} are driver wrappers; when mis-generated they break with:
#   gcc/as: 114: exec: -o: not found
# Replace with thin wrappers to real x86_64-elf binutils.
set -euo pipefail

BUILD="${BFREE_ELF_GCC_BUILD_DIR:-${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/gcc-build}"
GCC_DIR="$BUILD/gcc"
PREFIX="${BFREE_ELF_GCC_PREFIX:-${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-gcc-full}"

BFREE_ROOT="${BFREE_BFREE_X86_64_ROOT:-${BFREE_ROOT:-}}"
[[ -n "$BFREE_ROOT" ]] || {
  echo "fix_gcc_as_ld_wrappers: set BFREE_ROOT or BFREE_BFREE_X86_64_ROOT" >&2
  exit 1
}
# shellcheck source=bfree_elf_binutils.sh
. "$BFREE_ROOT/tools/bfree_elf_binutils.sh"

BINUTILS="$(bfree_find_elf_binutils_dir)" || {
  echo "fix_gcc_as_ld_wrappers: x86_64-elf-as/ld not found" >&2
  bfree_print_binutils_help
  exit 1
}

AS="$BINUTILS/x86_64-elf-as"
LD="$BINUTILS/x86_64-elf-ld"

[[ -d "$GCC_DIR" ]] || { echo "missing $GCC_DIR (run make all-gcc first)" >&2; exit 1; }

wrap() {
  local name="$1" tool="$2"
  if [[ -f "$GCC_DIR/$name" && ! -f "$GCC_DIR/$name.broken-wrapper" ]]; then
    cp -a "$GCC_DIR/$name" "$GCC_DIR/$name.broken-wrapper"
  fi
  printf '%s\n' '#!/bin/sh' "exec \"$tool\" \"\$@\"" >"$GCC_DIR/$name"
  chmod +x "$GCC_DIR/$name"
  echo "[fix-gcc-wrapper] $GCC_DIR/$name -> $tool"
}

wrap as "$AS"
wrap ld "$LD"
# collect-ld is used during libstdc++ configure link probes
if [[ -f "$GCC_DIR/collect-ld" ]]; then
  wrap collect-ld "$LD"
fi

echo "[fix-gcc-wrapper] smoke test:"
cat >/tmp/bfree-wrap-test.c <<'EOF'
int x;
EOF
"$GCC_DIR/xgcc" -B"$GCC_DIR/" -c -o /tmp/bfree-wrap-test.o /tmp/bfree-wrap-test.c
ls -la /tmp/bfree-wrap-test.o
