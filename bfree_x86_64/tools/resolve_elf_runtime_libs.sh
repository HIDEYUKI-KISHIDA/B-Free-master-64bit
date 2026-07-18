#!/usr/bin/env bash
# Resolve libc.a / libm.a / libgcc.a / libstdc++.a / crt for x86_64-elf guest link (-nostdlib).
set -euo pipefail

CC="${1:-$(command -v x86_64-elf-gcc 2>/dev/null || true)}"
CXX="${2:-$(command -v x86_64-elf-g++ 2>/dev/null || true)}"
GCC_ROOT="${BFREE_ELF_GCC_ROOT:-${BFREE_ELF_GCC_PREFIX:-${HOME}/bfree-native-build/x86_64-elf-gcc-full}}"
SCRIPT_ROOT="${BFREE_ROOT:-}"
if [[ -z "$SCRIPT_ROOT" && -n "${BASH_SOURCE[0]:-}" && "${BASH_SOURCE[0]}" != bash ]]; then
  SCRIPT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
MUSL_OUT="${BFREE_ELF_LIBM_DIR:-${SCRIPT_ROOT}/out/x86_64-elf-libm}"
DEFAULT_BFREE_LIBC="${MUSL_OUT}/libc.a"
DEFAULT_BFREE_LIBM="${MUSL_OUT}/libm.a"

# Optional explicit paths (see tools/install_x86_64_elf_libstdcxx.sh)
if [[ -n "${BFREE_ELF_LIBSTDCXX_PATH:-}" && -f "${BFREE_ELF_LIBSTDCXX_PATH}" ]]; then
  export _BFREE_LIBSTDCXX_PRESET="${BFREE_ELF_LIBSTDCXX_PATH}"
fi
if [[ -n "${BFREE_ELF_LIBSUPCXX_PATH:-}" && -f "${BFREE_ELF_LIBSUPCXX_PATH}" ]]; then
  export _BFREE_LIBSUPCXX_PRESET="${BFREE_ELF_LIBSUPCXX_PATH}"
fi

# Reject host MinGW/Linux libstdc++ that x86_64-elf-g++ sometimes reports on /mnt/c PATH.
is_guest_elf_archive() {
  local p="$1"
  [[ -f "$p" ]] || return 1
  case "$p" in
    *msys*|*mingw*|*i686-w64*|*x86_64-linux-gnu*|*i386-linux-gnu*) return 1 ;;
  esac
  case "$p" in
    */lib/gcc/x86_64-elf/*|*/x86_64-elf/lib/*|*/x86_64-elf-gcc-full/lib/*) return 0 ;;
  esac
  if [[ -n "${BFREE_ELF_GCC_PREFIX:-}" && "$p" == "${BFREE_ELF_GCC_PREFIX}/lib/"* ]]; then
    return 0
  fi
  if [[ -n "${BFREE_ELF_GCC_ROOT:-}" && "$p" == "${BFREE_ELF_GCC_ROOT}/lib/"* ]]; then
    return 0
  fi
  return 1
}

find_one() {
  local name="$1"
  local cc="$2"
  local p d r ver roots=() found

  if [[ -n "$cc" && -x "$cc" ]]; then
    p="$("$cc" -print-file-name="$name" 2>/dev/null || true)"
    if [[ -n "$p" && -f "$p" ]] && { [[ "$name" != "libstdc++.a" && "$name" != "libsupc++.a" ]] || is_guest_elf_archive "$p"; }; then
      echo "$p"
      return 0
    fi
    p="$("$cc" -print-file-name=libgcc.a 2>/dev/null || true)"
    if [[ -f "$p" ]]; then
      d="$(dirname "$p")"
      if [[ -f "$d/$name" ]]; then
        echo "$d/$name"
        return 0
      fi
    fi
    d="$("$cc" -print-file-name=libstdc++.a 2>/dev/null || true)"
    if [[ "$name" == "libstdc++.a" && -n "$d" && -f "$d" ]] && is_guest_elf_archive "$d"; then
      echo "$d"
      return 0
    fi
  fi

  [[ -n "$GCC_ROOT" ]] && roots+=("$GCC_ROOT")
  if [[ -n "$cc" ]]; then
    roots+=("$(cd "$(dirname "$cc")/.." && pwd)")
    roots+=("$(cd "$(dirname "$cc")/../.." && pwd)")
  fi
  roots+=(
    "${BFREE_ELF_GCC_PREFIX:-${SCRIPT_ROOT}/out/x86_64-elf-gcc-full}"
    "${BFREE_ELF_RUNTIME_DIR:-${SCRIPT_ROOT}/third_party/x86_64-elf-runtime}"
    "${BFREE_X86_64_ELF_TOOLS:-}"
    "${HOME}/x86_64-elf-toolchain"
    "/root/x86_64-elf-toolchain"
    "/usr/local/x86_64-elf"
    "/usr/lib/gcc/x86_64-elf"
    "/usr/x86_64-elf"
    "/usr"
    "${MUSL_OUT}"
  )
  if [[ -n "${SCRIPT_ROOT:-}" ]]; then
    roots+=("${SCRIPT_ROOT}/out/x86_64-elf-libm")
  fi

  for r in "${roots[@]}"; do
    [[ -n "$r" && -d "$r" ]] || continue
    for ver in "$r/lib/gcc/x86_64-elf"/*; do
      [[ -f "$ver/$name" ]] || continue
      echo "$ver/$name"
      return 0
    done
  done

  for r in "${roots[@]}"; do
    [[ -n "$r" && -d "$r" ]] || continue
    for p in "$r/x86_64-elf/lib/$name" "$r/lib/$name" "$r/$name"; do
      if [[ -f "$p" ]]; then
        if [[ "$name" == "libstdc++.a" || "$name" == "libsupc++.a" ]]; then
          is_guest_elf_archive "$p" || continue
        fi
        echo "$p"
        return 0
      fi
    done
  done

  while IFS= read -r -d '' p; do
    if [[ "$name" == "libstdc++.a" || "$name" == "libsupc++.a" ]]; then
      is_guest_elf_archive "$p" || continue
    fi
    echo "$p"
    return 0
  done < <(find "${roots[@]}" \( -path '*/msys64/*' -o -path '*/mingw*/*' \) -prune -o -name "$name" -print0 2>/dev/null)
  return 1
}

[[ -n "$CC" ]] || { echo "resolve_elf_runtime_libs: x86_64-elf-gcc not in PATH" >&2; exit 1; }

LIBC="$(find_one libc.a "$CC" || true)"
if [[ -z "$LIBC" && -f "${BFREE_ELF_LIBC_PATH:-$DEFAULT_BFREE_LIBC}" ]]; then
  LIBC="${BFREE_ELF_LIBC_PATH:-$DEFAULT_BFREE_LIBC}"
fi

LIBM="$(find_one libm.a "$CC" || true)"
if [[ -z "$LIBM" && -f "${BFREE_ELF_LIBM_PATH:-$DEFAULT_BFREE_LIBM}" ]]; then
  LIBM="${BFREE_ELF_LIBM_PATH:-$DEFAULT_BFREE_LIBM}"
fi

if [[ -z "$LIBC" ]]; then
  echo "resolve_elf_runtime_libs: libc.a not found (CC=$CC GCC_ROOT=$GCC_ROOT)" >&2
  echo "  lordmilko x86_64-elf-tools has gcc/g++ only (no libc)." >&2
  echo "  Build musl libc: bash tools/build_x86_64_elf_libm.sh   (~5-15 min)" >&2
  exit 1
fi

LIBGCC="$(find_one libgcc.a "$CC" || true)"
[[ -n "$LIBGCC" ]] || LIBGCC="$(find_one libgcc.a "$CXX" || true)"
if [[ -z "$LIBGCC" ]]; then
  echo "resolve_elf_runtime_libs: libgcc.a not found (CC=$CC CXX=$CXX)" >&2
  exit 1
fi

LIBSTDCXX="${_BFREE_LIBSTDCXX_PRESET:-}"
LIBSUPC="${_BFREE_LIBSUPCXX_PRESET:-}"
if [[ -z "$LIBSTDCXX" && -n "$LIBGCC" && -f "$(dirname "$LIBGCC")/libstdc++.a" ]]; then
  LIBSTDCXX="$(dirname "$LIBGCC")/libstdc++.a"
fi
if [[ -z "$LIBSUPC" && -n "$LIBGCC" && -f "$(dirname "$LIBGCC")/libsupc++.a" ]]; then
  LIBSUPC="$(dirname "$LIBGCC")/libsupc++.a"
fi
if [[ -n "$CXX" ]]; then
  [[ -n "$LIBSTDCXX" ]] || LIBSTDCXX="$(find_one libstdc++.a "$CXX" || true)"
  [[ -n "$LIBSUPC" ]] || LIBSUPC="$(find_one libsupc++.a "$CXX" || true)"
fi
if [[ -z "$LIBSTDCXX" ]]; then
  echo "resolve_elf_runtime_libs: libstdc++.a not found (CXX=$CXX)" >&2
  echo "  $(command -v x86_64-elf-g++ 2>/dev/null || echo g++) -print-file-name=libstdc++.a =>" \
    "$("$CXX" -print-file-name=libstdc++.a 2>/dev/null || true)" >&2
  [[ -n "$LIBGCC" ]] && echo "  libgcc at: $LIBGCC (no libstdc++.a beside it)" >&2
  echo "  lordmilko zip is often gcc-only. Run:" >&2
  echo "    bash tools/build_x86_64_elf_libstdcxx.sh   (self-build, ~30-90 min)" >&2
  echo "    bash tools/install_x86_64_elf_libstdcxx.sh (apt copy)" >&2
  echo "  or: sudo apt install g++-x86-64-elf" >&2
  exit 1
fi

CRT_END=""
if [[ -n "$CXX" ]]; then
  CRT_END="$("$CXX" -print-file-name=crtendS.o 2>/dev/null || true)"
  [[ -f "$CRT_END" ]] || CRT_END=""
fi

# Link order (static): Qt archives, then compat.o, then C++ runtime group, libgcc, crtend.
out="-Wl,--start-group"
out+=" $LIBSTDCXX"
# libsupc++.a is not listed here: this install's libstdc++.a already contains the same
# libsupc++ object files; linking both causes multiple-definition errors at guest link.
out+=" $LIBC"
if [[ -n "$LIBM" && -f "$LIBM" && "$LIBM" != "$LIBC" && "$(stat -c%s "$LIBM" 2>/dev/null || echo 0)" -gt 64 ]]; then
  out+=" $LIBM"
fi
out+=" -Wl,--end-group $LIBGCC"
[[ -n "$CRT_END" ]] && out+=" $CRT_END"
echo "$out"
