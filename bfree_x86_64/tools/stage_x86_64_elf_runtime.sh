#!/usr/bin/env bash
# Stage x86_64-elf libstdc++.a (+ libsupc++.a) for formal distribution under third_party/.
# Writes MANIFEST with sha256. Does not commit to git (see .gitignore).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME_DIR="${BFREE_ELF_RUNTIME_DIR:-$ROOT/third_party/x86_64-elf-runtime}"
LIB_DIR="$RUNTIME_DIR/lib"
MANIFEST="$RUNTIME_DIR/MANIFEST"
GCC_VER="${BFREE_ELF_GCC_VER:-13}"

mkdir -p "$LIB_DIR"

is_elf_archive() {
  local p="$1"
  [[ -f "$p" ]] || return 1
  case "$p" in
    *msys*|*mingw*|*x86_64-linux-gnu*) return 1 ;;
  esac
  case "$p" in
    */lib/gcc/x86_64-elf/*|*/x86_64-elf/lib/*) return 0 ;;
  esac
  return 1
}

stage_one() {
  local src="$1" name="$2"
  is_elf_archive "$src" || return 1
  cp -f "$src" "$LIB_DIR/$name"
  echo "[runtime] staged $LIB_DIR/$name <= $src"
}

find_newest() {
  local name="$1"
  find "${BFREE_ELF_GCC_PREFIX:-$ROOT/out/x86_64-elf-gcc-full}" \
    /usr/lib/gcc/x86_64-elf "${HOME}/x86_64-elf-toolchain" /root/x86_64-elf-toolchain \
    /usr/local/x86_64-elf "${BFREE_X86_64_ELF_TOOLS:-}" \
    -path "*/lib/gcc/x86_64-elf/*/$name" -type f 2>/dev/null \
    | while read -r p; do is_elf_archive "$p" && echo "$p"; done | head -n 1
}

write_manifest() {
  {
    echo "# B-Free x86_64-elf guest runtime manifest"
    echo "# generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "runtime_dir=$RUNTIME_DIR"
    echo "target_triple=x86_64-elf"
    echo "gcc_version_expected=$GCC_VER"
    if command -v x86_64-elf-gcc >/dev/null; then
      echo "toolchain_gcc=$(x86_64-elf-gcc --version | head -n 1)"
    fi
    if command -v dpkg-query >/dev/null; then
      dpkg-query -W 'g++-x86-64-elf' 2>/dev/null | while read -r line; do
        echo "debian_package=$line"
      done || true
    fi
    echo ""
    for f in "$LIB_DIR"/*.a; do
      [[ -f "$f" ]] || continue
      echo "file=$(basename "$f")"
      echo "path=$f"
      echo "bytes=$(stat -c%s "$f" 2>/dev/null || wc -c <"$f")"
      echo "sha256=$(sha256sum "$f" | awk '{print $1}')"
      echo ""
    done
  } >"$MANIFEST"
  sha256sum "$LIB_DIR"/*.a 2>/dev/null >"$RUNTIME_DIR/MANIFEST.sha256" || true
  echo "[runtime] wrote $MANIFEST"
}

# --- apt extract (reproducible on Debian/Ubuntu) ---
stage_from_apt() {
  command -v apt-get >/dev/null || return 1
  local tmp deb
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' RETURN
  echo "[runtime] apt download g++-x86-64-elf ..."
  (cd "$tmp" && apt-get download g++-x86-64-elf 2>/dev/null) || return 1
  deb=$(echo "$tmp"/g++-x86-64-elf_*.deb)
  [[ -f "$deb" ]] || return 1
  mkdir -p "$tmp/extract"
  dpkg-deb -x "$deb" "$tmp/extract"
  local std sup
  std=$(find "$tmp/extract" -path '*/lib/gcc/x86_64-elf/*/libstdc++.a' | head -n 1)
  sup=$(find "$tmp/extract" -path '*/lib/gcc/x86_64-elf/*/libsupc++.a' | head -n 1)
  [[ -n "$std" ]] && stage_one "$std" libstdc++.a
  [[ -n "$sup" ]] && stage_one "$sup" libsupc++.a
}

# --- main ---
if [[ -f "$LIB_DIR/libstdc++.a" ]] && [[ "${BFREE_RUNTIME_FORCE:-0}" != 1 ]]; then
  echo "[runtime] already staged: $LIB_DIR/libstdc++.a (BFREE_RUNTIME_FORCE=1 to refresh)"
  write_manifest
  exit 0
fi

stage_from_apt || true

if [[ ! -f "$LIB_DIR/libstdc++.a" ]]; then
  std=$(find_newest libstdc++.a)
  [[ -n "$std" ]] && stage_one "$std" libstdc++.a
fi
if [[ ! -f "$LIB_DIR/libsupc++.a" ]]; then
  sup=$(find_newest libsupc++.a)
  [[ -n "$sup" ]] && stage_one "$sup" libsupc++.a
fi

if [[ ! -f "$LIB_DIR/libstdc++.a" ]]; then
  echo "[runtime] ERROR: could not stage libstdc++.a" >&2
  echo "  Install: sudo apt install g++-x86-64-elf" >&2
  echo "  Or set BFREE_ELF_LIBSTDCXX_PATH to a valid x86_64-elf libstdc++.a" >&2
  exit 1
fi

cp -f "$ROOT/third_party/x86_64-elf-runtime/GCC-RUNTIME-NOTICE.txt" "$RUNTIME_DIR/" 2>/dev/null || true
write_manifest
echo "[runtime] OK — use: export BFREE_ELF_RUNTIME_DIR=$RUNTIME_DIR"
