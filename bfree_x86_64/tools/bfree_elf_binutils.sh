# shellcheck shell=bash
# Shared x86_64-elf binutils discovery (ELF on WSL; skip PE32 /root/bin).
bfree_binutils_tool_ok() {
  local p="$1"
  [[ -x "$p" && -s "$p" ]] || return 1
  case "$(file -b -L "$p" 2>/dev/null || true)" in
    *ELF\ 64-bit*) return 0 ;;
    *PE32*|*MS\ Windows*) return 1 ;;
  esac
  return 1
}

bfree_find_elf_binutils_dir() {
  local d
  for d in \
    "${BFREE_ELF_BINUTILS_DIR:-}" \
    "${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-binutils/bin" \
    "${BFREE_ELF_GCC_PREFIX:-${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-gcc-full}/x86_64-elf/bin" \
    /usr/bin \
    /usr/local/x86_64-elf/bin \
    /usr/local/bin \
    "${HOME}/x86_64-elf-toolchain/bin" \
    /root/bin; do
    [[ -n "$d" ]] || continue
    [[ -x "$d/x86_64-elf-as" && -x "$d/x86_64-elf-ld" ]] || continue
    bfree_binutils_tool_ok "$d/x86_64-elf-as" && bfree_binutils_tool_ok "$d/x86_64-elf-ld" || continue
    echo "$d"
    return 0
  done
  return 1
}

bfree_print_binutils_help() {
  echo "No ELF x86_64-elf-as / x86_64-elf-ld found." >&2
  echo "Ubuntu 24.04 (noble) has no apt binutils-x86-64-elf package (removed vs 22.04)." >&2
  echo "  /root/bin and /usr/local lordmilko copies are often PE32 on WSL — do not use for link." >&2
  echo "Build ELF binutils (recommended):" >&2
  echo "  sudo apt install -y build-essential flex bison texinfo wget file" >&2
  echo "  export BFREE_LINUX_BUILD_ROOT=\${BFREE_LINUX_BUILD_ROOT:-\$HOME/bfree-native-build}" >&2
  echo "  bash tools/build_x86_64_elf_binutils.sh" >&2
  echo "  export BFREE_ELF_BINUTILS_DIR=\$BFREE_LINUX_BUILD_ROOT/x86_64-elf-binutils/bin" >&2
  echo "On Ubuntu 22.04 only: sudo apt install -y binutils-x86-64-elf  # then BFREE_ELF_BINUTILS_DIR=/usr/bin" >&2
  echo "Diagnostics:" >&2
  local c d
  for d in \
    "${BFREE_ELF_BINUTILS_DIR:-}" \
    /usr/bin \
    /usr/local/x86_64-elf/bin \
    "${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-binutils/bin" \
    "${BFREE_ELF_GCC_PREFIX:-${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-gcc-full}/x86_64-elf/bin" \
    /root/bin; do
    [[ -n "$d" ]] || continue
    for c in "$d/x86_64-elf-as" "$d/x86_64-elf-ld"; do
      printf '  %s: ' "$c" >&2
      if [[ ! -e "$c" ]]; then
        echo 'missing' >&2
      else
        file -b -L "$c" 2>/dev/null >&2 || echo '?' >&2
      fi
    done
  done
}

bfree_resolve_elf_ld() {
  local d
  [[ -n "${BFREE_ELF_LD:-}" && -x "${BFREE_ELF_LD}" ]] && bfree_binutils_tool_ok "${BFREE_ELF_LD}" && {
    echo "${BFREE_ELF_LD}"
    return 0
  }
  if d="$(bfree_find_elf_binutils_dir)"; then
    echo "$d/x86_64-elf-ld"
    return 0
  fi
  return 1
}
