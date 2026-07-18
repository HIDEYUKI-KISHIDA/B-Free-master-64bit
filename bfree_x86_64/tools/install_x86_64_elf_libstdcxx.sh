#!/usr/bin/env bash
# Install libstdc++.a (+ libsupc++.a) for x86_64-elf next to your libgcc.a.
# lordmilko x86_64-elf-tools zip often ships gcc/g++/libgcc only — no libstdc++.a.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc 2>/dev/null || true)}"
CXX="${BFREE_ELF_CXX:-$(command -v x86_64-elf-g++ 2>/dev/null || true)}"
[[ -n "$CC" && -x "$CC" ]] || { echo "x86_64-elf-gcc not in PATH" >&2; exit 1; }

LIBGCC="$("$CC" -print-file-name=libgcc.a 2>/dev/null || true)"
[[ -f "$LIBGCC" ]] || { echo "libgcc.a not found (CC=$CC)" >&2; exit 1; }
DEST="$(cd "$(dirname "$LIBGCC")" && pwd)"
echo "[libstdc++] target dir (next to libgcc): $DEST"

if [[ -f "$DEST/libstdc++.a" ]]; then
  echo "[libstdc++] already present: $DEST/libstdc++.a"
  ls -la "$DEST/libstdc++.a" "$DEST/libsupc++.a" 2>/dev/null || true
  exit 0
fi

copy_if_elf() {
  local src="$1"
  local base="$2"
  local allow_any="${3:-0}"
  [[ -f "$src" ]] || return 1
  case "$src" in
    *msys*|*mingw*|*x86_64-linux-gnu*) return 1 ;;
  esac
  if [[ "$allow_any" != 1 ]]; then
    case "$src" in
      */lib/gcc/x86_64-elf/*|*/x86_64-elf/lib/*|*/x86_64-elf-gcc-full/lib/*) ;;
      *) return 1 ;;
    esac
  fi
  cp -f "$src" "$DEST/$base"
  echo "[libstdc++] installed $DEST/$base <= $src"
  return 0
}

try_find() {
  local f
  while IFS= read -r -d '' f; do
    copy_if_elf "$f" "$(basename "$f")" && return 0
  done < <(find "$@" \( -path '*/msys64/*' -o -path '*/mingw*/*' -o -path '*x86_64-linux-gnu*' \) -prune \
    -o \( -name 'libstdc++.a' -o -name 'libsupc++.a' \) -print0 2>/dev/null)
  return 1
}

# 1) Explicit override (trusted paths from our GCC build)
[[ -n "${BFREE_ELF_LIBSTDCXX_PATH:-}" && -f "${BFREE_ELF_LIBSTDCXX_PATH}" ]] && \
  copy_if_elf "${BFREE_ELF_LIBSTDCXX_PATH}" "libstdc++.a" 1
[[ -n "${BFREE_ELF_LIBSUPCXX_PATH:-}" && -f "${BFREE_ELF_LIBSUPCXX_PATH}" ]] && \
  copy_if_elf "${BFREE_ELF_LIBSUPCXX_PATH}" "libsupc++.a" 1
[[ -f "$DEST/libstdc++.a" ]] && { ls -la "$DEST"/libstdc++.a "$DEST"/libsupc++.a 2>/dev/null; exit 0; }

# 2) Common install trees
try_find /root/bfree-native-build/x86_64-elf-gcc-full/lib \
  /root/bfree-native-build/x86_64-elf-gcc-full/lib/gcc/x86_64-elf \
  "${BFREE_ELF_GCC_PREFIX:-/root/bfree-native-build/x86_64-elf-gcc-full}/lib/gcc/x86_64-elf" \
  /usr/lib/gcc/x86_64-elf /usr/x86_64-elf/lib \
  "${HOME}/x86_64-elf-toolchain" /root/x86_64-elf-toolchain /usr/local/x86_64-elf \
  "${BFREE_X86_64_ELF_TOOLS:-}" "$ROOT/out" || true
[[ -f "$DEST/libstdc++.a" ]] && { ls -la "$DEST"/libstdc++.a; exit 0; }

# 3) apt (Debian/Ubuntu WSL)
if command -v apt-get >/dev/null; then
  echo "[libstdc++] trying: sudo apt-get install -y g++-x86-64-elf"
  if sudo apt-get install -y g++-x86-64-elf 2>/dev/null; then
    try_find /usr/lib/gcc/x86_64-elf /usr/x86_64-elf || true
  fi
fi
[[ -f "$DEST/libstdc++.a" ]] && { ls -la "$DEST"/libstdc++.a; exit 0; }

echo "[libstdc++] ERROR: no x86_64-elf libstdc++.a found." >&2
echo "  Your g++ returns only a basename:" >&2
echo "    $(command -v x86_64-elf-g++ 2>/dev/null || echo '?') -print-file-name=libstdc++.a" >&2
echo "    => $("$CXX" -print-file-name=libstdc++.a 2>/dev/null || true)" >&2
echo "" >&2
echo "  Fix options:" >&2
echo "    1) sudo apt install g++-x86-64-elf   then re-run this script" >&2
echo "    2) Use a full x86_64-elf toolchain (with lib/gcc/x86_64-elf/*/libstdc++.a)" >&2
echo "    3) export BFREE_ELF_LIBSTDCXX_PATH=/path/to/libstdc++.a" >&2
echo "       export BFREE_ELF_LIBSUPCXX_PATH=/path/to/libsupc++.a  # optional" >&2
exit 1
