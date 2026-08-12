#!/usr/bin/env bash
# Meson cross-build wrapper: musl sysroot + drop -pthread (lordmilko x86_64-elf-gcc).
set -euo pipefail
REAL_GCC="${BFREE_ELF_CC:-x86_64-elf-gcc}"
SYSROOT="${BFREE_ELF_MUSL_SYSROOT:-}"
args=()
for a in "$@"; do
  [[ "$a" == -pthread ]] && continue
  args+=("$a")
done
if [[ -n "$SYSROOT" ]]; then
  exec "$REAL_GCC" \
    -I"$SYSROOT/include" \
    -L"$SYSROOT/lib" \
    "${args[@]}"
fi
exec "$REAL_GCC" "${args[@]}"
