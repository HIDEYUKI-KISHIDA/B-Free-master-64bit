#!/usr/bin/env bash
# Put x86_64-elf-g++ on PATH (lordmilko prebuilt or bfree-native-build gcc-full).
# Rejects broken /usr/local/x86_64-elf (Windows PE as → Exec format error on WSL).
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
#
#   source tools/ensure_x86_64_elf_toolchain.sh
#   eval "$(BFREE_ROOT=/path/to/bfree_x86_64 bash tools/ensure_x86_64_elf_toolchain.sh --export)"

set -euo pipefail

EXPORT_ONLY=0
if [[ "${1:-}" == "--export" ]]; then
  EXPORT_ONLY=1
fi

if [[ -n "${BFREE_ROOT:-}" ]]; then
  ROOT="$(cd "$BFREE_ROOT" && pwd)"
else
  _self="${BASH_SOURCE[0]:-$0}"
  if [[ "$_self" == bash || "$_self" == -bash || "$_self" == /*bash ]]; then
    _self=""
  fi
  if [[ -n "$_self" ]]; then
    ROOT="$(cd "$(dirname "$_self")/.." && pwd)"
  else
    ROOT="$(pwd)"
  fi
fi
export BFREE_ROOT="$ROOT"
HOME_DIR="${HOME:-/root}"

toolchain_works() {
  local bindir="$1"
  local gcc="$bindir/x86_64-elf-gcc"
  local gxx="$bindir/x86_64-elf-g++"
  local as ft

  [[ -x "$gcc" && -x "$gxx" ]] || return 1
  as="$("$gcc" -print-prog-name=as 2>/dev/null || true)"
  [[ -n "$as" && -x "$as" ]] || return 1
  if command -v file >/dev/null 2>&1; then
    ft="$(file -b "$as" 2>/dev/null || true)"
    case "$ft" in
      *PE32* | *MS\ Windows* | *Windows*) return 1 ;;
    esac
  fi
  printf 'int bfree_tc(void){return 0;}\n' | "$gcc" -xc -c - -o /dev/null 2>/dev/null
}

toolchain_candidate_bins() {
  local d seen="" key
  for d in \
    "/root/x86_64-elf-toolchain" \
    "$HOME_DIR/x86_64-elf-toolchain" \
    "${BFREE_X86_64_ELF_TOOLS:-}" \
    "${BFREE_ELF_GCC_ROOT:-}" \
    "${BFREE_ELF_GCC_PREFIX:-}" \
    "$HOME_DIR/bfree-native-build/x86_64-elf-gcc-full" \
    "/root/bfree-native-build/x86_64-elf-gcc-full" \
    "$ROOT/out/x86_64-elf-gcc-full" \
    "/usr/local/x86_64-elf" \
    "/usr/local"; do
    [[ -n "$d" ]] || continue
    key="$d"
    case " $seen " in *" $key "*) continue ;; esac
    seen="$seen $key"
    if [[ -x "$d/bin/x86_64-elf-g++" ]]; then
      echo "$d/bin"
    elif [[ -x "$d/x86_64-elf-g++" ]]; then
      echo "$d"
    fi
  done
}

BIN_DIR=""
while IFS= read -r _cand; do
  [[ -n "$_cand" ]] || continue
  if toolchain_works "$_cand"; then
    BIN_DIR="$_cand"
    break
  fi
done < <(toolchain_candidate_bins)

if [[ -z "$BIN_DIR" ]] && command -v x86_64-elf-g++ >/dev/null 2>&1; then
  _cur="$(dirname "$(command -v x86_64-elf-g++)")"
  if toolchain_works "$_cur"; then
    BIN_DIR="$_cur"
  else
    echo "[elf-toolchain] WARN: $(command -v x86_64-elf-g++) on PATH fails compile test" >&2
    echo "  (often /usr/local/x86_64-elf Windows tools — use /root/x86_64-elf-toolchain/bin)" >&2
  fi
fi

if [[ -z "$BIN_DIR" ]]; then
  if [[ "$EXPORT_ONLY" == "1" ]]; then
    echo "echo '[elf-toolchain] FAIL: no working x86_64-elf-g++ found' >&2" >&2
    echo "exit 1"
    exit 1
  fi
  echo "[elf-toolchain] FAIL: no working x86_64-elf-g++ (lordmilko / native gcc-full)." >&2
  echo "  Install:" >&2
  echo "    bash $ROOT/tools/install_x86_64_elf_gpp_prebuilt.sh" >&2
  echo "  Then:" >&2
  echo "    export PATH=\"/root/x86_64-elf-toolchain/bin:\$PATH\"" >&2
  exit 1
fi

GXX="$BIN_DIR/x86_64-elf-g++"
GCC_ROOT="$(cd "$BIN_DIR/.." && pwd)"
export BFREE_ELF_GCC_ROOT="$GCC_ROOT"
export BFREE_X86_64_ELF_TOOLS="$GCC_ROOT"
export BFREE_ELF_CC="$BIN_DIR/x86_64-elf-gcc"
export BFREE_ELF_CXX="$GXX"
export PATH="$BIN_DIR:${PATH:-}"

if [[ "$EXPORT_ONLY" == "1" ]]; then
  printf 'export PATH="%s";\n' "$BIN_DIR:${PATH:-}"
  printf 'export BFREE_ELF_GCC_ROOT="%s";\n' "$BFREE_ELF_GCC_ROOT"
  printf 'export BFREE_X86_64_ELF_TOOLS="%s";\n' "$BFREE_X86_64_ELF_TOOLS"
  printf 'export BFREE_ELF_CC="%s";\n' "$BFREE_ELF_CC"
  printf 'export BFREE_ELF_CXX="%s";\n' "$BFREE_ELF_CXX"
  exit 0
fi

echo "[elf-toolchain] x86_64-elf-g++: $GXX"
"$GXX" --version 2>/dev/null | head -1 || true
