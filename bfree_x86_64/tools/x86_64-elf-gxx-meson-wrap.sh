#!/usr/bin/env bash
# Meson cross-build wrapper: musl sysroot + drop -pthread (lordmilko x86_64-elf-g++).
set -euo pipefail
REAL_GXX="${BFREE_ELF_CXX:-x86_64-elf-g++}"
SYSROOT="${BFREE_ELF_MUSL_SYSROOT:-}"
args=()
for a in "$@"; do
  [[ "$a" == -pthread ]] && continue
  args+=("$a")
done
if [[ -n "$SYSROOT" ]]; then
  exec "$REAL_GXX" \
    -I"$SYSROOT/include" \
    -L"$SYSROOT/lib" \
    "${args[@]}"
fi
exec "$REAL_GXX" "${args[@]}"
