#!/usr/bin/env bash
# Manual libstdc++-v3 configure for x86_64-elf + musl (do NOT use make configure-target-*).
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LINUX_ROOT="${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}"
BUILD="${BFREE_ELF_GCC_BUILD_DIR:-$LINUX_ROOT/gcc-build}"
SRC="${BFREE_GCC_SRC_ROOT:-$LINUX_ROOT/gcc-src}/gcc-${BFREE_GCC_VERSION:-13.2.0}"
PREFIX="${BFREE_ELF_GCC_PREFIX:-$LINUX_ROOT/x86_64-elf-gcc-full}"
SYSROOT="${BFREE_ELF_MUSL_SYSROOT:-$ROOT/out/x86_64-elf-libm/prefix}"
# Linking against drvfs (/mnt/c) often fails or drops -o output; stage on ext4 unless opted out.
if [[ "${BFREE_SKIP_MUSL_SYSROOT_STAGE:-0}" != 1 ]] && [[ "$SYSROOT" == /mnt/* ]]; then
  STAGED="/root/bfree-musl-sysroot"
  if [[ ! -f "$STAGED/lib/libc.a" ]] || [[ "$SYSROOT/lib/libc.a" -nt "$STAGED/lib/libc.a" ]]; then
    echo "[libstdc++-manual] staging musl sysroot $SYSROOT -> $STAGED (ext4)"
    rm -rf "$STAGED"
    cp -a "$SYSROOT" "$STAGED"
  fi
  SYSROOT="$STAGED"
fi
# shellcheck source=bfree_elf_binutils.sh
. "$ROOT/tools/bfree_elf_binutils.sh"
if ! BINUTILS="$(bfree_find_elf_binutils_dir)"; then
  bfree_print_binutils_help
  exit 1
fi
export BFREE_ELF_BINUTILS_DIR="$BINUTILS"
LIBDIR="$BUILD/x86_64-elf/libstdc++-v3"
TGT_BIN="$PREFIX/x86_64-elf/bin"
SITE="$ROOT/tools/libstdcxx-config.site"
LOG="$BUILD/libstdcxx-manual-configure.log"

# Run helpers from /tmp with LF only (/mnt/c CRLF breaks "set -o pipefail").
run_lf() {
  local s="$1"
  local d="/tmp/bfree-$(basename "$s").$$"
  tr -d '\r' <"$s" >"$d"
  chmod +x "$d"
  bash "$d"
  rm -f "$d"
}

export PATH="$BINUTILS:/usr/bin:/bin"
export BFREE_BFREE_X86_64_ROOT="$ROOT"
run_lf "$ROOT/tools/fix_gcc_as_ld_wrappers.sh"
# Always refresh configure from backup + patch (avoid stale partial patches).
run_lf "$ROOT/tools/patch_libstdcxx_configure_link_tests.sh"

[[ -x "$BUILD/gcc/xgcc" ]] || { echo "run make all-gcc first" >&2; exit 1; }
[[ -f "$BUILD/x86_64-elf/libgcc/libgcc.a" || -f "$PREFIX/lib/gcc/x86_64-elf/"*/libgcc.a ]] || {
  echo "run make all-target-libgcc first" >&2
  exit 1
}

mkdir -p "$TGT_BIN"
for t in as ld ar nm ranlib strip objcopy objdump readelf; do
  p="$BINUTILS/x86_64-elf-$t"
  [[ -x "$p" ]] || continue
  case "$t" in
    as|ld) bfree_binutils_tool_ok "$p" || continue ;;
  esac
  dest="$TGT_BIN/x86_64-elf-$t"
  if [[ ! -e "$dest" ]] || ! cmp -s "$p" "$dest"; then
    install -m 755 "$p" "$dest"
  fi
done

mkdir -p "$SYSROOT/usr" "$SYSROOT/usr/include/linux" "$SYSROOT/include/linux"
[[ -e "$SYSROOT/usr/include" && ! -L "$SYSROOT/usr/include" ]] || ln -sfn ../include "$SYSROOT/usr/include"
[[ -e "$SYSROOT/usr/lib" && ! -L "$SYSROOT/usr/lib" ]] || ln -sfn ../lib "$SYSROOT/usr/lib"
printf '%s\n' '#include <stdint.h>' >"$SYSROOT/usr/include/linux/types.h"
printf '%s\n' '#include <stdint.h>' >"$SYSROOT/include/linux/types.h"
echo "[libstdc++-manual] sysroot linux/types.h OK"

MUSL_FIX="/tmp/bfree-fix_gcc_musl_link.$$"
tr -d '\r' <"$ROOT/tools/fix_gcc_musl_link.sh" >"$MUSL_FIX"
chmod +x "$MUSL_FIX"
bash "$MUSL_FIX" "$SYSROOT"
rm -f "$MUSL_FIX"
export LIBRARY_PATH="${BFREE_MUSL_LIBRARY_PATH:-$SYSROOT/lib}"

GCC_VER_DIR="$PREFIX/lib/gcc/x86_64-elf/${BFREE_GCC_VERSION:-13.2.0}"
LIBGCC_BUILD="$BUILD/x86_64-elf/libgcc"
LIBGCC_A="$GCC_VER_DIR/libgcc.a"
[[ -f "$LIBGCC_A" ]] || LIBGCC_A="$LIBGCC_BUILD/libgcc.a"
[[ -f "$LIBGCC_A" ]] || { echo "[libstdc++-manual] ERROR: libgcc.a not found" >&2; exit 1; }
LIBC_A="$SYSROOT/lib/libc.a"

# Need native ELF ld. /root/bin (lordmilko) is often PE32 on WSL: exits 0 but writes no output file.
if ! ELF_LD="$(bfree_resolve_elf_ld)"; then
  echo "[libstdc++-manual] ERROR: no ELF x86_64-elf-ld found." >&2
  bfree_print_binutils_help
  exit 1
fi
echo "[libstdc++-manual] ELF_LD=$ELF_LD ($(file -b "$ELF_LD"))"

WRAP_DIR="$BUILD/bfree-wrappers"
mkdir -p "$WRAP_DIR"
WRAP_CC="$WRAP_DIR/x86_64-elf-cc"
WRAP_CXX="$WRAP_DIR/x86_64-elf-c++"
TOOL_BIN="$(dirname "$ELF_LD")"
AS_BIN="$TOOL_BIN/x86_64-elf-as"
echo "[libstdc++-manual] libgcc=$LIBGCC_A"

# autoconf: "cc conftest.c" without -o → must produce ./conftest (not a.out).
write_wrapper() {
  local out="$1" driver="$2"
  cat >"$out" <<WRAP
#!/bin/bash
LOG=/tmp/bfree-wrap-cc.log
export LIBRARY_PATH="${BFREE_MUSL_LIBRARY_PATH:-$SYSROOT/lib:$GCC_VER_DIR:$LIBGCC_BUILD:$BUILD/gcc}"
echo "=== \$0 \$*" >>"\$LOG"
X="$BUILD/gcc/$driver"
LD="$ELF_LD"
AS="$AS_BIN"
# libstdc++ configure runs "\$CXX -v" and picks gthr-single vs gthr-posix from "Thread model:".
# Our x86_64-elf-g++ is "single" but musl has pthread — report posix for hosted libstdc++ only.
for _a in "\$@"; do
  if [[ "\$_a" == -v ]]; then
    "\$X" "\$@" 2>&1 | sed 's/^Thread model: single\$/Thread model: posix/'
    exit "\${PIPESTATUS[0]}"
  fi
done
# libtool (libstdc++): -c -std=gnu++11 ... foo.s -o foo.{lo,o} — must not pass .s to xg++/ld.
_bf_c=0 _bf_s= _bf_o= _bf_p=
for _bf_a in "\$@"; do
  [[ "\$_bf_a" == -c ]] && _bf_c=1
  case "\$_bf_a" in *.s) _bf_s="\$_bf_a" ;; esac
done
if (( _bf_c )) && [[ -n "\$_bf_s" ]]; then
  for _bf_a in "\$@"; do
    [[ "\$_bf_p" == -o ]] && _bf_o="\$_bf_a"
    _bf_p="\$_bf_a"
  done
  [[ -z "\$_bf_o" ]] && _bf_o="\${_bf_s%.s}.o"
  echo "bfree early as: \$AS -o \$_bf_o \$_bf_s" >>"\$LOG"
  exec "\$AS" -o "\$_bf_o" "\$_bf_s"
fi
# Explicit argv (unquoted "\$B \$L" breaks multi-flag -B/-L strings on some shells).
xgcc_base() {
  printf '%s\n' "-B$BUILD/gcc/" "-B$TGT_BIN/" -B"$PREFIX/x86_64-elf/lib/" \
    -isysroot "$SYSROOT"
}
xgcc_args() {
  xgcc_base
  printf '%s\n' -static \
    -L"$SYSROOT/lib" -L"$GCC_VER_DIR" -L"$LIBGCC_BUILD"
}
have_o=0
outfile=""
prev=""
for a in "\$@"; do
  [[ "\$prev" == -o ]] && outfile="\$a"
  [[ "\$a" == -o ]] && have_o=1
  prev="\$a"
done
musl_link_tail() {
  printf '%s\n' "$BUILD/gcc/crtbegin.o" \
    "$LIBC_A" "$LIBGCC_A" \
    "$BUILD/gcc/crtend.o" "$SYSROOT/lib/crtn.o"
}
is_compile_only() {
  for a in "\$@"; do
    case "\$a" in
      -c|-S|-E|-M|-MM|-MP|-MD|-MMD|-P|-fsyntax-only|-xnone|-xassembler) return 0 ;;
    esac
  done
  return 1
}
musl_ld_link_objs() {
  local out="\$1"
  shift
  "\$LD" -m elf_x86_64 -static -o "\$out" \
    "$SYSROOT/lib/crt1.o" "$SYSROOT/lib/crti.o" "\$@" \
    --start-group "$LIBC_A" "$LIBGCC_A" --end-group \
    "$SYSROOT/lib/crtn.o"
  [[ -s "\$out" ]] || { echo "musl_ld_link_objs: missing \$out" >>"\$LOG"; return 1; }
}
musl_ld_link() {
  local out="\$1"
  shift
  local o args=() a skip=0
  for a in "\$@"; do
    if (( skip )); then skip=0; continue; fi
    [[ "\$a" == -o ]] && { skip=1; continue; }
    args+=("\$a")
  done
  o="\$(mktemp /tmp/bfree-wrap-XXXXXX.o)"
  "\$X" \$(xgcc_args) -nostdlib -c "\${args[@]}" -o "\$o"
  musl_ld_link_objs "\$out" "\$o"
  rm -f "\$o"
}
has_src=0
has_obj=0
for a in "\$@"; do
  case "\$a" in *.c|*.cc|*.cpp|*.cxx|*.C) has_src=1 ;; *.o) has_obj=1 ;; esac
done
if (( have_o && has_obj && ! has_src )); then
  echo "musl_ld_link_objs \$outfile: \$*" >>"\$LOG"
  objs=() skip=0
  for a in "\$@"; do
    if (( skip )); then skip=0; continue; fi
    [[ "\$a" == -o ]] && { skip=1; continue; }
    case "\$a" in -static|-nostdlib) continue ;; esac
    objs+=("\$a")
  done
  musl_ld_link_objs "\$outfile" "\${objs[@]}"
  exit \$?
fi
# -E / -c must not hit musl_ld_link (conftest.c name would wrongly link).
if is_compile_only "\$@"; then
  # libtool: -c -std=gnu++11 foo.s -o foo.lo  (do not assume the path follows -c)
  # -S -o foo.s writes assembly text — do not treat the .s path as assembler input.
  _bf_has_c=0
  for _a in "\$@"; do
    [[ "\$_a" == -c ]] && _bf_has_c=1 && break
  done
  asm_src= asm_out= _prev=
  if (( _bf_has_c )); then
    for _a in "\$@"; do
      case "\$_a" in
        *.s) asm_src="\$_a" ;;
        *.S|*.sx) asm_src="\$_a" ;;
      esac
      [[ "\$_prev" == -o ]] && asm_out="\$_a"
      _prev="\$_a"
    done
  fi
  if [[ -n "\$asm_src" ]]; then
    case "\$asm_src" in
      *.s)
        if [[ -z "\$asm_out" ]]; then
          asm_out="\${asm_src%.s}.o"
        fi
        echo "exec as -c: $TOOL_BIN/x86_64-elf-as -o \$asm_out \$asm_src" >>"\$LOG"
        exec "$TOOL_BIN/x86_64-elf-as" -o "\$asm_out" "\$asm_src"
        ;;
      *.S|*.sx)
        echo "exec asm-cpp -c: xgcc -x assembler-with-cpp \$*" >>"\$LOG"
        exec "$BUILD/gcc/xgcc" \$(xgcc_base) -x assembler-with-cpp "\$@"
        ;;
    esac
  fi
  echo "exec compile/cpp: \$X + args + \$*" >>"\$LOG"
  exec "\$X" \$(xgcc_base) "\$@"
fi
case " \$* " in
  *" conftest.c"*|*" conftest.cpp"*|*" conftest.cxx"*|*" conftest.cc"*)
    if (( ! have_o )) || [[ "\$outfile" == conftest ]]; then
      echo "musl_ld_link conftest: \$*" >>"\$LOG"
      musl_ld_link conftest "\$@"
      exit \$?
    fi
    ;;
esac
echo "exec musl link: \$X + args + \$*" >>"\$LOG"
exec "\$X" \$(xgcc_args) -nostdlib \
  "$SYSROOT/lib/crt1.o" "$SYSROOT/lib/crti.o" \
  "\$@" \$(musl_link_tail)
WRAP
  sed -i 's/\r$//' "$out" 2>/dev/null || true
  chmod +x "$out"
}
write_wrapper "$WRAP_CC" "xgcc"
write_wrapper "$WRAP_CXX" "xg++"
bash -n "$WRAP_CC" && bash -n "$WRAP_CXX"
grep -q 'bfree early as' "$WRAP_CXX" || {
  echo "[libstdc++-manual] ERROR: wrapper missing early .s handler" >&2
  exit 1
}
echo "[libstdc++-manual] wrappers: $WRAP_CC (bash)"

if [[ "${BFREE_REFRESH_WRAPPERS_ONLY:-0}" == 1 ]]; then
  echo "[libstdc++-manual] wrappers refreshed only (BFREE_REFRESH_WRAPPERS_ONLY=1)"
  if [[ -f "$LIBDIR/src/c++11/Makefile" ]]; then
    export BFREE_LINUX_BUILD_ROOT="$LINUX_ROOT"
    run_lf "$ROOT/tools/patch_libstdcxx_makefile_asm.sh" || true
  fi
  exit 0
fi

rm -rf "$LIBDIR"
mkdir -p "$LIBDIR"
cd "$LIBDIR"

# conftest.c without -o must create ./conftest in the configure directory (like autoconf).
cat >"$LIBDIR/conftest.c" <<'EOF'
int main(void) { return 0; }
EOF
rm -f "$LIBDIR/conftest" "$LIBDIR/a.out"
: >/tmp/bfree-conftest.log
: >/tmp/bfree-wrap-cc.log

# Link with x86_64-elf-ld + explicit .a (collect2 still adds /mnt/c -L and may drop output).
try_conftest_link() {
  local ec=0
  (
    set -e
    cd "$LIBDIR"
    rm -f conftest conftest.o
    "$BUILD/gcc/xgcc" \
      -B"$BUILD/gcc/" -B"$TGT_BIN/" -B"$PREFIX/x86_64-elf/lib/" \
      -isysroot "$SYSROOT" -nostdlib -static -c -g -O2 \
      -o conftest.o conftest.c
    echo "[conftest] xgcc -c OK"
    ld_ec=0
    "$ELF_LD" -m elf_x86_64 -static -o "$LIBDIR/conftest" \
      "$SYSROOT/lib/crt1.o" "$SYSROOT/lib/crti.o" "$LIBDIR/conftest.o" \
      --start-group "$LIBC_A" "$LIBGCC_A" --end-group \
      "$SYSROOT/lib/crtn.o" || ld_ec=$?
    ls -la "$LIBDIR/conftest.o" "$LIBDIR/conftest"
    if [[ $ld_ec -ne 0 || ! -s "$LIBDIR/conftest" ]]; then
      echo "[conftest] ld failed ec=$ld_ec ELF_LD=$ELF_LD" >&2
      exit 1
    fi
    echo "[conftest] ld OK size=$(stat -c%s "$LIBDIR/conftest")"
  ) >>/tmp/bfree-conftest.log 2>&1 || ec=$?
  echo "[conftest] try_conftest_link exit=$ec" >>/tmp/bfree-conftest.log
  [[ $ec -eq 0 && -f "$LIBDIR/conftest" ]]
}

if ! try_conftest_link; then
  echo "[libstdc++-manual] ERROR: musl ld link to conftest failed:" >&2
  tail -50 /tmp/bfree-conftest.log >&2
  exit 1
fi
echo "[libstdc++-manual] ld creates $LIBDIR/conftest OK"

# Wrapper must also produce conftest (autoconf uses CC=wrapper).
rm -f "$LIBDIR/conftest"
if ! (cd "$LIBDIR" && "$WRAP_CC" -g -O2 conftest.c 2>>/tmp/bfree-conftest.log); then
  echo "[libstdc++-manual] WARN: wrapper failed; configure may still use direct xgcc paths" >&2
  tail -20 /tmp/bfree-conftest.log >&2
elif [[ ! -f "$LIBDIR/conftest" ]]; then
  echo "[libstdc++-manual] WARN: wrapper exit 0 but no conftest — see /tmp/bfree-wrap-cc.log" >&2
  cat /tmp/bfree-wrap-cc.log >&2 || true
else
  echo "[libstdc++-manual] wrapper creates $LIBDIR/conftest OK"
fi

# Quick link test before long configure
cat >/tmp/bfree-cc-test.c <<'EOF'
int main(void) { return 0; }
EOF
if ! "$WRAP_CC" -o /tmp/bfree-cc-test /tmp/bfree-cc-test.c 2>/tmp/bfree-cc-test.log; then
  echo "[libstdc++-manual] ERROR: wrapper link test failed — /tmp/bfree-cc-test.log:" >&2
  tail -20 /tmp/bfree-cc-test.log >&2
  rm -f "$WRAP_CC" "$WRAP_CXX"
  exit 1
fi
echo "[libstdc++-manual] wrapper link test OK: /tmp/bfree-cc-test"

SITE_TMP="/tmp/bfree-libstdcxx-config.site.$$"
tr -d '\r' <"$SITE" >"$SITE_TMP"
export CONFIG_SITE="$SITE_TMP"

# Skip autoconf link probe (wrapper already verified); CRLF on /mnt/c breaks site vars.
cat >"$LIBDIR/config.cache" <<CACHE
ac_cv_c_compiler_works=yes
ac_cv_prog_cc_cross=yes
ac_cv_prog_cxx_cross=yes
ac_cv_prog_cc_g=yes
ac_cv_prog_cxx_g=yes
ac_cv_cxx_compiler_gnu=yes
cross_compiling=yes
libstdcxx_cv_output_filetype=elf64-x86-64
lt_cv_prog_compiler_c_static_works=yes
lt_cv_prog_compiler_cxx_static_works=yes
libstdcxx_cv_threads=yes
libstdcxx_cv_mutex_native=yes
libstdcxx_cv_gthr_posix=yes
enable_libstdcxx_pch=no
glibcxx_cv_prog_CXX_pch=no
ac_cv_search_shl_load=no
ac_cv_func_shl_load=no
ac_cv_lib_dld_shl_load=no
ac_cv_prog_AS_R=no
CACHE
echo "[libstdc++-manual] seed $LIBDIR/config.cache"

echo "[libstdc++-manual] configure in $LIBDIR (hosted=yes, musl sysroot, threads=posix)"

cross_compiling=yes \
ac_cv_c_compiler_works=yes \
ac_cv_prog_cc_cross=yes \
ac_cv_prog_cxx_cross=yes \
libstdcxx_cv_output_filetype=elf64-x86-64 \
  "$SRC/libstdc++-v3/configure" \
  --srcdir="$SRC/libstdc++-v3" \
  --build=x86_64-pc-linux-gnu \
  --host=x86_64-pc-elf \
  --target=x86_64-pc-elf \
  --prefix="$PREFIX" \
  --enable-hosted-libstdcxx \
  --disable-libstdcxx-pch \
  --disable-shared \
  --enable-static \
  --disable-multilib \
  --enable-clocale=generic \
  --with-gxx-include-dir="$PREFIX/include/c++/${BFREE_GCC_VERSION:-13.2.0}" \
  CC="$WRAP_CC" \
  CXX="$WRAP_CXX" \
  CPP="${BUILD}/gcc/xgcc -B${BUILD}/gcc/ -B${TGT_BIN}/ -B${PREFIX}/x86_64-elf/lib/ -isysroot ${SYSROOT} -E" \
  AS="${TOOL_BIN}/x86_64-elf-as" \
  AR="${TOOL_BIN}/x86_64-elf-ar" \
  RANLIB="${TOOL_BIN}/x86_64-elf-ranlib" \
  NM="${TOOL_BIN}/x86_64-elf-nm" \
  OBJDUMP="${TOOL_BIN}/x86_64-elf-objdump" \
  LD="$ELF_LD" \
  STRIP="${TOOL_BIN}/x86_64-elf-strip" \
  CFLAGS="-g -O2 -D__linux__" \
  CXXFLAGS="-g -O2 -D__linux__" \
  LDFLAGS="-static -L$SYSROOT/lib -L$GCC_VER_DIR" \
  CONFIG_SITE="$SITE_TMP" \
  2>&1 | tee "$LOG"

rm -f "$SITE_TMP"

for msg in 'Link tests are not allowed' 'No support for this host/target combination'; do
  if grep -q "$msg" "$LOG"; then
    echo "[libstdc++-manual] ERROR: $msg" >&2
    exit 1
  fi
done

[[ -f "$LIBDIR/Makefile" ]] || {
  echo "[libstdc++-manual] ERROR: Makefile not created — see $LOG" >&2
  if [[ -f "$LIBDIR/config.log" ]]; then
    echo "=== config.log (C compiler) ===" >&2
    grep -A25 'checking whether the C compiler works' "$LIBDIR/config.log" | tail -30 >&2 || true
  fi
  exit 1
}

# musl wrapper often makes "checking for gthreads library" fail even when gthr-posix is selected.
run_lf "$ROOT/tools/force_libstdcxx_gthreads.sh" || bash "$ROOT/tools/force_libstdcxx_gthreads.sh"

export BFREE_LINUX_BUILD_ROOT="$LINUX_ROOT"
export BFREE_ELF_GCC_BUILD_DIR="$BUILD"
run_lf "$ROOT/tools/patch_libstdcxx_makefile_asm.sh" || bash "$ROOT/tools/patch_libstdcxx_makefile_asm.sh"

echo "[libstdc++-manual] OK — wrappers kept at $WRAP_DIR (used by Makefile)"
echo "  cd $BUILD && make -j\$(nproc) all-target-libstdc++-v3 && make install-target-libstdc++-v3"
