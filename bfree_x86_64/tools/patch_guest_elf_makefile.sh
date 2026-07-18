#!/usr/bin/env bash
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail
MF="${1:-Makefile.guest-elf}"
[ -f "$MF" ] || { echo "missing $MF" >&2; exit 1; }
QT_PFX="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
sed -i \
  -e "s|${QT_PFX}/plugins/networkinformation/libqglib.a||g" \
  -e "s|${QT_PFX}/plugins/tls/libqcertonlybackend.a||g" \
  -e "s|${QT_PFX}/lib/libQt6Network.a||g" \
  -e "s| ${QT_PFX}/ | |g" \
  -e 's/-lglib[^ ]*//g' \
  -e 's/-lgthread[^ ]*//g' \
  -e 's/-lgobject[^ ]*//g' \
  -e 's/-lgio[^ ]*//g' \
  -e 's/-licui18n//g' \
  -e 's/-licuuc//g' \
  -e 's/-licudata//g' \
  -e 's/-lpcre2-16//g' \
  -e 's/-lbrotlidec//g' \
  -e 's/-lresolv//g' \
  -e 's/-ldl//g' \
  -e 's/-lrt//g' \
  -e 's/-lpthread//g' \
  -e 's/-lm//g' \
  "$MF"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
guest_cc="$(command -v x86_64-elf-gcc 2>/dev/null || true)"
guest_cxx="$(command -v x86_64-elf-g++ 2>/dev/null || true)"
ELF_RUNTIME="$(BFREE_ROOT="$ROOT" BFREE_ELF_GCC_ROOT="${BFREE_ELF_GCC_ROOT:-$HOME}" \
  BFREE_ELF_LIBM_DIR="${BFREE_ELF_LIBM_DIR:-$ROOT/out/x86_64-elf-libm}" \
  BFREE_X86_64_ELF_TOOLS="${BFREE_X86_64_ELF_TOOLS:-$HOME/x86_64-elf-toolchain}" \
  bash "$ROOT/tools/resolve_elf_runtime_libs.sh" "$guest_cc" "$guest_cxx")"
COMPAT="$(cd "$(dirname "$MF")" && pwd)/guest_link_compat.o"
case "$ELF_RUNTIME" in *libstdc++*) ;; *)
  echo "[patch] ERROR: libstdc++.a missing — run: x86_64-elf-g++ -print-file-name=libstdc++.a" >&2
  exit 1
  ;;
esac
sed -i "s#\(libQt6BundledPcre2\.a\)#\1 ${COMPAT} ${ELF_RUNTIME}#g" "$MF"
if ! grep -q 'guest_link_compat.o' "$MF"; then
  sed -i "s#\( -o[[:space:]]*desktop\)# ${COMPAT} ${ELF_RUNTIME}\1#g" "$MF"
fi
grep -q 'guest_link_compat.o' "$MF" || { echo "[patch] ERROR: inject failed for $MF" >&2; exit 1; }
echo "[patch] stripped host glib/icu/network libs from $MF"
echo "[patch] runtime libs: ${ELF_RUNTIME}"
