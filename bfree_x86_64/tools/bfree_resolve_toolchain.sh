#!/usr/bin/env bash
# Resolve Linux x86_64-elf gcc + binutils (skip PE /usr/local broken copies).
# Usage: source tools/bfree_resolve_toolchain.sh

bfree_tool_ok() {
  local p="$1"
  test -x "$p" && test -s "$p" || return 1
  case "$(file -b -L "$p" 2>/dev/null || true)" in
    *ELF\ 64-bit*) return 0 ;;
  esac
  return 1
}

bfree_resolve_gcc_root() {
  local base d
  for base in \
    "${BFREE_ELF_GCC_ROOT:-}" \
    "${BFREE_ELF_GCC_PREFIX:-}" \
    "${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-gcc-full" \
    "/root/bfree-native-build/x86_64-elf-gcc-full" \
    "$HOME/bfree-native-build/x86_64-elf-gcc-full" \
    "$HOME/out/x86_64-elf-gcc-full"; do
    test -n "$base" || continue
    if bfree_tool_ok "$base/bin/x86_64-elf-gcc"; then
      echo "$base"
      return 0
    fi
    if bfree_tool_ok "$base/x86_64-elf/bin/x86_64-elf-gcc"; then
      echo "$base"
      return 0
    fi
  done
  return 1
}

bfree_resolve_binutils_dir() {
  local d
  for d in \
    "${BFREE_ELF_BINUTILS_DIR:-}" \
    "${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-binutils/bin" \
    "/root/bfree-native-build/x86_64-elf-binutils/bin" \
    "$HOME/bfree-native-build/x86_64-elf-binutils/bin"; do
    test -n "$d" || continue
    if bfree_tool_ok "$d/x86_64-elf-as" && bfree_tool_ok "$d/x86_64-elf-ld"; then
      echo "$d"
      return 0
    fi
  done
  local gcc_root
  gcc_root="$(bfree_resolve_gcc_root 2>/dev/null || true)"
  if test -n "$gcc_root"; then
    for d in "$gcc_root/bin" "$gcc_root/x86_64-elf/bin"; do
      if bfree_tool_ok "$d/x86_64-elf-as" && bfree_tool_ok "$d/x86_64-elf-ld"; then
        echo "$d"
        return 0
      fi
    done
  fi
  return 1
}

bfree_apply_toolchain_env() {
  local gcc_root binutils
  gcc_root="$(bfree_resolve_gcc_root)" || {
    echo "[bfree] ERROR: no Linux x86_64-elf-gcc found." >&2
    echo "  Set BFREE_ELF_GCC_ROOT to your prefix (directory containing bin/x86_64-elf-gcc)." >&2
    echo "  Common paths:" >&2
    echo "    \$HOME/bfree-native-build/x86_64-elf-gcc-full" >&2
    echo "    /root/bfree-native-build/x86_64-elf-gcc-full" >&2
    echo "  Build: bash tools/build_x86_64_elf_libstdcxx.sh  (see tools/build_x86_64_elf_binutils.sh)" >&2
    return 1
  }
  binutils="$(bfree_resolve_binutils_dir)" || {
    echo "[bfree] ERROR: no ELF x86_64-elf-as/ld next to gcc ($gcc_root)." >&2
    echo "  bash tools/build_x86_64_elf_binutils.sh" >&2
    return 1
  }
  local gcc_bin="$gcc_root/bin"
  if ! bfree_tool_ok "$gcc_bin/x86_64-elf-gcc"; then
    gcc_bin="$gcc_root/x86_64-elf/bin"
  fi
  export BFREE_ELF_GCC_ROOT="$gcc_root"
  export BFREE_ELF_BINUTILS_DIR="$binutils"
  export BFREE_ELF_LIBSTDCXX_PATH="${BFREE_ELF_LIBSTDCXX_PATH:-$gcc_root/lib/libstdc++.a}"
  _path_clean="$(echo "${PATH:-}" | tr ':' '\n' | grep -v '/usr/local/x86_64-elf' | paste -sd: -)"
  export PATH="$gcc_bin:$binutils:$_path_clean"
  echo "[bfree] BFREE_ELF_GCC_ROOT=$gcc_root"
  echo "[bfree] BFREE_ELF_BINUTILS_DIR=$binutils"
  echo "[bfree] CC=$gcc_bin/x86_64-elf-gcc"
  file "$("$gcc_bin/x86_64-elf-gcc" -print-prog-name=as)" 2>/dev/null || true
  return 0
}
