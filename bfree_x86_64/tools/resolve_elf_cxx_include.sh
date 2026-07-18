#!/usr/bin/env bash
# Print libstdc++ include dir (must contain chrono) for guest_link_compat.cpp / guest-elf.
# lordmilko x86_64-elf-g++ often has libgcc only; libstdc++.a may live under bfree-native-build.
set -euo pipefail

ROOT="${BFREE_ROOT:-}"
if [[ -z "$ROOT" && -n "${BASH_SOURCE[0]:-}" && "${BASH_SOURCE[0]}" != bash ]]; then
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi

has_chrono() {
  [[ -n "${1:-}" && -f "$1/chrono" ]]
}

# 1) Explicit override
if has_chrono "${BFREE_ELF_CXX_INCLUDE:-}"; then
  echo "${BFREE_ELF_CXX_INCLUDE}"
  exit 0
fi

# 2) Beside libstdc++.a from resolve_elf_runtime_libs (same gcc version tree)
CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc 2>/dev/null || true)}"
CXX="${BFREE_ELF_CXX:-$(command -v x86_64-elf-g++ 2>/dev/null || true)}"
if [[ -n "$CC" || -n "$CXX" ]]; then
  _rt="$(bash "${BASH_SOURCE%/*}/resolve_elf_runtime_libs.sh" "$CC" "$CXX" 2>/dev/null || true)"
  _std="$(echo "$_rt" | tr ' ' '\n' | grep -E 'libstdc\+\+\.a$' | head -1 || true)"
  if [[ -n "$_std" && -f "$_std" ]]; then
    _ver="$(dirname "$_std")"
    if has_chrono "$_ver/include/c++"; then
      echo "$_ver/include/c++"
      exit 0
    fi
    _libroot="$(cd "$_ver/.." 2>/dev/null && pwd || true)"
    for _cand in \
      "$_libroot"/lib/gcc/x86_64-elf/*/include/c++ \
      "$_libroot"/include/c++/13.2.0 \
      "$_libroot"/include/c++/*; do
      if has_chrono "$_cand"; then
        echo "$_cand"
        exit 0
      fi
    done
  fi
fi

# 3) BFREE_ELF_LIBSTDCXX_PATH override
if [[ -n "${BFREE_ELF_LIBSTDCXX_PATH:-}" && -f "${BFREE_ELF_LIBSTDCXX_PATH}" ]]; then
  _ver="$(dirname "${BFREE_ELF_LIBSTDCXX_PATH}")"
  if has_chrono "$_ver/include/c++"; then
    echo "$_ver/include/c++"
    exit 0
  fi
fi

# 4) Common full-toolchain trees (bfree-native-build, apt install)
_search_roots=(
  "${BFREE_ELF_GCC_ROOT:-}"
  "${BFREE_ELF_GCC_PREFIX:-}"
  "${HOME}/bfree-native-build/x86_64-elf-gcc-full"
  "/root/bfree-native-build/x86_64-elf-gcc-full"
  "${ROOT}/out/x86_64-elf-gcc-full"
  "${HOME}/x86_64-elf-toolchain"
  "/root/x86_64-elf-toolchain"
  "/usr/local/x86_64-elf"
  "/usr"
)
for _r in "${_search_roots[@]}"; do
  [[ -n "$_r" && -d "$_r" ]] || continue
  for _cand in \
    "$_r"/lib/gcc/x86_64-elf/*/include/c++ \
    "$_r"/include/c++/13.2.0 \
    "$_r"/include/c++/*; do
    if has_chrono "$_cand"; then
      echo "$_cand"
      exit 0
    fi
  done
done

# 5) g++ -print-file-name (if full toolchain)
if [[ -n "$CXX" && -x "$CXX" ]]; then
  _pfx="$(cd "$(dirname "$CXX")/.." && pwd)"
  for _cand in \
    "$_pfx"/lib/gcc/x86_64-elf/*/include/c++ \
    "$_pfx"/include/c++/13.2.0 \
    "$_pfx"/include/c++/*; do
    if has_chrono "$_cand"; then
      echo "$_cand"
      exit 0
    fi
  done
fi

echo "resolve_elf_cxx_include: no include/c++ with chrono (guest_link_compat needs <chrono>)" >&2
echo "  libstdc++.a may exist without headers (lordmilko zip). Try:" >&2
echo "    sudo apt install g++-x86-64-elf" >&2
echo "    bash tools/build_x86_64_elf_libstdcxx.sh   # full gcc + headers under ~/bfree-native-build" >&2
echo "  Then: export BFREE_ELF_CXX_INCLUDE=\$(bash tools/resolve_elf_cxx_include.sh)" >&2
exit 1
