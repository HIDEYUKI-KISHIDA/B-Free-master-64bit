#!/usr/bin/env bash
# libstdc++ configure probes <linux/types.h> (musl sysroot does not ship it).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SYSROOT="${BFREE_ELF_MUSL_SYSROOT:-$ROOT/out/x86_64-elf-libm/prefix}"
mkdir -p "$SYSROOT/usr/include/linux" "$SYSROOT/include/linux"
printf '%s\n' '#include <stdint.h>' >"$SYSROOT/usr/include/linux/types.h"
printf '%s\n' '#include <stdint.h>' >"$SYSROOT/include/linux/types.h"
echo "[sysroot-linux] $SYSROOT/{include,usr/include}/linux/types.h"
