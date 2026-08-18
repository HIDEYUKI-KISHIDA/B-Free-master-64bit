#!/usr/bin/env bash
# qmake LINK wrapper: crt0.o first + musl/libstdc++ via x86_64-elf-ld (same as stub desktop.elf).
# On /mnt/c (CRLF): guest_desktop_link_qmake.sh pipes this file through tr -d '\r'.
set -euo pipefail

REAL_CXX="${BFREE_GUEST_LINK_CXX:-$(command -v x86_64-elf-g++ 2>/dev/null || true)}"
[[ -n "$REAL_CXX" && -x "$REAL_CXX" ]] || {
  echo "guest_desktop_link: x86_64-elf-g++ not found (set BFREE_GUEST_LINK_CXX)" >&2
  exit 1
}
REAL_LINK="${BFREE_GUEST_CC:-}"
if [[ -z "$REAL_LINK" || ! -x "$REAL_LINK" ]]; then
  REAL_LINK="${REAL_CXX/g++/gcc}"
  [[ -x "$REAL_LINK" ]] || REAL_LINK="$REAL_CXX"
fi
LD="${BFREE_ELF_LD:-}"
if [[ -z "$LD" && -n "${BFREE_ELF_BINUTILS_DIR:-}" ]]; then
  LD="${BFREE_ELF_BINUTILS_DIR}/x86_64-elf-ld"
fi
[[ -z "$LD" || ! -x "$LD" ]] && LD="$(command -v x86_64-elf-ld 2>/dev/null || true)"

ROOT="${BFREE_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
CRT0="${BFREE_GUEST_CRT0:-$ROOT/userland/desktop_qt/crt0.o}"
COMPAT="${BFREE_GUEST_COMPAT:-$ROOT/userland/desktop_qt/guest_link_compat.o}"
GUEST_SERIAL="${BFREE_GUEST_SERIAL:-$ROOT/userland/desktop_qt/guest_serial.o}"
GUEST_MMAP="${BFREE_GUEST_MMAP:-$ROOT/userland/desktop_qt/guest_mmap.o}"
CC="${BFREE_GUEST_CC:-$(command -v x86_64-elf-gcc 2>/dev/null || true)}"
CXX="${BFREE_GUEST_CXX:-$REAL_CXX}"

_resolve() {
  bash -c 'tr -d "\r" < "$1" | bash -s -- "$2" "$3"' _ \
    "$ROOT/tools/resolve_elf_runtime_libs.sh" "$CC" "$CXX"
}

RUNTIME_STR="$(_resolve)"
read -ra RUNTIME <<< "$RUNTIME_STR"
[[ ${#RUNTIME[@]} -gt 0 ]] || { echo "guest_desktop_link: resolve_elf_runtime_libs failed" >&2; exit 1; }
case " ${RUNTIME[*]} " in *libstdc++*) ;; *)
  echo "guest_desktop_link: libstdc++.a missing in: ${RUNTIME[*]}" >&2
  exit 1
  ;;
esac
[[ -f "$COMPAT" ]] || {
  echo "guest_desktop_link: missing $COMPAT (run: make -f Makefile.bfree guest_link_compat.o)" >&2
  exit 1
}
[[ -f "$CRT0" ]] || {
  echo "guest_desktop_link: missing $CRT0 (run: make -f Makefile.bfree crt0.o)" >&2
  exit 1
}

# Qt Qml built with linux-g++ still references QNetwork*; pull Network archive if present.
GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-}"
append_qt_network_if_needed() {
  local a net
  [[ -n "$GUEST_QT" ]] || return 0
  net="$GUEST_QT/lib/libQt6Network.a"
  [[ -f "$net" ]] || return 0
  for a in "${archives[@]}"; do
    [[ "$a" == *libQt6Qml.a ]] && break
  done
  [[ "$a" == *libQt6Qml.a ]] || return 0
  case " ${archives[*]} " in
    *libQt6Network.a*) return 0 ;;
  esac
  archives+=("$net")
  echo "[guest_desktop_link] + $net (Qml needs QNetwork symbols)" >&2
}

# Convert -Wl,--start-group to ld-native flags when using x86_64-elf-ld directly.
runtime_ld=()
for r in "${RUNTIME[@]}"; do
  case "$r" in
    -Wl,--start-group) runtime_ld+=(--start-group) ;;
    -Wl,--end-group) runtime_ld+=(--end-group) ;;
    -Wl,*) runtime_ld+=("${r#-Wl,}") ;;
    *) runtime_ld+=("$r") ;;
  esac
done

# Strip CR from argv (Makefile.guest-elf / qmake on /mnt/c often pass -o\r).
args=()
for _a in "$@"; do
  args+=("${_a//$'\r'/}")
done

OUT=""
SCRIPT=""
inputs=()
objs=()
archives=()
for ((i = 0; i < ${#args[@]}; i++)); do
  a="${args[i]}"
  case "$a" in
    -o)
      OUT="${args[i + 1]}"
      ((i++))
      continue
      ;;
    -o*)
      OUT="${a#-o}"
      [[ -n "$OUT" ]] || { OUT="${args[i + 1]}"; ((i++)); }
      continue
      ;;
    -T)
      SCRIPT="${args[i + 1]}"
      ((i++))
      continue
      ;;
    -T*)
      SCRIPT="${a#-T}"
      continue
      ;;
    -nostdlib | -nodefaultlibs | -Wl,-O1)
      continue
      ;;
    -Wl,-O*)
      continue
      ;;
    -l*)
      continue
      ;;
  esac
  [[ "$a" == -* ]] && continue
  [[ "$a" == *.ld ]] && { SCRIPT="$a"; continue; }
  [[ "$a" == *crt0.o ]] && continue
  [[ "$a" == *guest_link_compat.o ]] && continue
  [[ "$a" == *guest_serial.o ]] && continue;
  inputs+=("$a")
  if [[ "$a" == *.a ]]; then
    case "$a" in
      *libstdc++.a | *libsupc++.a | *libc.a | *libgcc.a) continue ;;
    esac
    archives+=("$a")
  else
    objs+=("$a")
  fi
done

append_qt_network_if_needed

# libqbfree.a is a static archive: without --whole-archive the guest event dispatcher
# (QEventDispatcherBFreeGuest) is often dropped and QGuiApplication hangs on Unix epoll.
qpa_whole_archive=()
archives_grouped=()
for a in "${archives[@]}"; do
  case "$a" in
    *libqbfree.a)
      archives_grouped+=(--whole-archive "$a" --no-whole-archive)
      qpa_whole_archive+=("$a")
      ;;
    *)
      archives_grouped+=("$a")
      ;;
  esac
done
if [[ ${#qpa_whole_archive[@]} -gt 0 ]]; then
  echo "[guest_desktop_link] --whole-archive ${qpa_whole_archive[*]}" >&2
fi

libstdcxx_a=""
libsupcxx_a=""
libc_a=""
libm_a=""
libgcc_a=""
crtend_o=""
for r in "${runtime_ld[@]}"; do
  case "$r" in
    --start-group | --end-group) ;;
    *libgcc.a) libgcc_a="$r" ;;
    *libstdc++.a) libstdcxx_a="$r" ;;
    *libsupc++.a) libsupcxx_a="$r" ;;
    *libc.a) libc_a="$r" ;;
    *libm.a) libm_a="$r" ;;
    *crtend*.o) crtend_o="$r" ;;
  esac
done
[[ -n "$libgcc_a" && -n "$libstdcxx_a" && -n "$libc_a" ]] || {
  echo "guest_desktop_link: runtime libs incomplete (need libstdc++.a libc.a libgcc.a)" >&2
  exit 1
}

# guest_mmap.o provides B-Free __mmap (no Linux syscall-9 fallback). Stock libc.a also has mmap.o.
guest_libc_for_link() {
  local src="$1"
  if [[ ! -f "$GUEST_MMAP" ]]; then
    echo "$src"
    return 0
  fi
  local ar_bin objcopy_bin
  ar_bin="$(command -v x86_64-elf-ar 2>/dev/null || command -v ar 2>/dev/null || true)"
  objcopy_bin="$(command -v x86_64-elf-objcopy 2>/dev/null || command -v objcopy 2>/dev/null || true)"
  [[ -n "$ar_bin" ]] || {
    echo "guest_desktop_link: x86_64-elf-ar not found (needed to drop libc mmap.o)" >&2
    return 1
  }
  [[ -n "$objcopy_bin" ]] || {
    echo "guest_desktop_link: x86_64-elf-objcopy not found (needed for guest malloc meta)" >&2
    return 1
  }
  local cache_dir="${BFREE_GUEST_LIBC_CACHE_DIR:-${TMPDIR:-/tmp}/bfree-guest-libc}"
  mkdir -p "$cache_dir"
  local cache="${cache_dir}/$(echo "$src" | cksum | awk '{print $1}')-nommap-noerrno.a"
  if [[ ! -f "$cache" || "$src" -nt "$cache" || "$GUEST_MMAP" -nt "$cache" || "$COMPAT" -nt "$cache" ]]; then
    cp "$src" "$cache"
    "$ar_bin" d "$cache" mmap.o 2>/dev/null || true
    "$ar_bin" d "$cache" __errno_location.o 2>/dev/null || true
    "$ar_bin" d "$cache" lock.o 2>/dev/null || true
    "$ar_bin" d "$cache" syscall.o 2>/dev/null || true
    echo "[guest_desktop_link] libc without mmap/errno/lock (+ guest stubs) -> $cache" >&2
  fi
  echo "$cache"
}
if ! nm -C "$libstdcxx_a" 2>/dev/null | grep -qE 'std::condition_variable|__gthread_mutex_t'; then
  if ! nm "$libstdcxx_a" 2>/dev/null | grep -qE 'condition_variable|__gthread_'; then
    echo "guest_desktop_link: WARNING: $libstdcxx_a has no condition_variable (libstdc++ built with threads=no)" >&2
    echo "  Rebuild: bash tools/force_libstdcxx_gthreads.sh && make clean in libstdc++-v3" >&2
  fi
fi

[[ -n "$OUT" ]] || {
  echo "guest_desktop_link: no -o output in link line" >&2
  exit 1
}
[[ -n "$SCRIPT" ]] || SCRIPT="$ROOT/userland/desktop_qt/desktop.ld"
[[ -f "$SCRIPT" ]] || {
  echo "guest_desktop_link: linker script missing: $SCRIPT" >&2
  exit 1
}

if [[ -n "$LD" && -x "$LD" ]]; then
  libc_link="$(guest_libc_for_link "$libc_a")" || exit 1
  echo "[guest_desktop_link] $LD -T $SCRIPT -e _start -o $OUT" \
    "objs=${#objs[@]} archives=${#archives[@]}" >&2
  runtime_group=("$libc_link")
  [[ -n "$libm_a" ]] && runtime_group+=("$libm_a")
  # Normal archive link for libstdc++ (not --whole-archive): whole-archive pulls
  # math_stubs_*.o that duplicate musl powf/sinf/... in libc.a.
  group=(--start-group "${archives_grouped[@]}" "${runtime_group[@]}" "$libstdcxx_a" --end-group)
  link_log="$(mktemp)"
  link_tail=()
  [[ -n "$crtend_o" ]] && link_tail+=("$crtend_o")
  link_tail+=("$libgcc_a")
  # __mmap lives in guest_link_compat.o (blocks anon mmap during Qt ctor).
  mmap_early=()
  wrap_ld=(--wrap=malloc --wrap=calloc --wrap=realloc --wrap=free --wrap=memcpy \
    --wrap=__libc_malloc --wrap=__libc_calloc --wrap=__libc_realloc --wrap=__libc_free \
    --wrap=_Znwm --wrap=_Znam --wrap=malloc_usable_size \
    --wrap=aligned_alloc --wrap=posix_memalign \
    --wrap=getenv --wrap=getuid --wrap=geteuid \
    --wrap=setlocale --wrap=nl_langinfo --wrap=getcwd --wrap=readlink \
    --wrap=gettimeofday --wrap=clock_gettime --wrap=nanosleep --wrap=stat --wrap=lstat \
    --wrap=printf --wrap=vprintf --wrap=fprintf --wrap=vfprintf \
    --wrap=open --wrap=opendir --wrap=realpath --wrap=abort \
    --wrap=poll --wrap=ppoll --wrap=epoll_wait --wrap=sched_yield --wrap=pthread_cond_wait --wrap=pthread_cond_timedwait \
    --wrap=pthread_create --wrap=pthread_once --wrap=pthread_self \
    --wrap=pthread_key_create --wrap=pthread_key_delete \
    --wrap=pthread_setspecific --wrap=pthread_getspecific \
    --wrap=pthread_mutex_lock --wrap=pthread_mutex_unlock \
    --wrap=pthread_mutex_init --wrap=pthread_cond_init \
    --wrap=_Unwind_RaiseException \
    --wrap=_Z21qRegisterResourceDataiPKhS0_S0_)
  stub_first=()
  main_objs=()
  for o in "${objs[@]}"; do
    case "$o" in
      *qt_futex_guest_stub.o|*guest_platform_stub.o) stub_first+=("$o") ;;
      *) main_objs+=("$o") ;;
    esac
  done
  if ! "$LD" -m elf_x86_64 -T "$SCRIPT" --no-undefined -e _start -z noexecstack \
    --allow-multiple-definition \
    -o "$OUT" \
    "$CRT0" "$COMPAT" "$GUEST_SERIAL" \
    "${mmap_early[@]}" \
    "${stub_first[@]}" \
    "${main_objs[@]}" \
    "${group[@]}" \
    "${link_tail[@]}" \
    "${wrap_ld[@]}" 2>"$link_log"; then
    echo "guest_desktop_link: link failed — errors (scroll up in make log for full list):" >&2
    grep -E 'undefined reference|multiple definition|ld: error:|ld: fatal' "$link_log" | tail -40 >&2 || true
    tail -15 "$link_log" >&2
    rm -f "$link_log"
    exit 1
  fi
  if grep -q 'lma .* adjusted' "$link_log"; then
    echo "[guest_desktop_link] note: TLS LMA adjust messages (usually harmless if link succeeded)" >&2
  fi
  rm -f "$link_log"
else
  libc_link="$(guest_libc_for_link "$libc_a")" || exit 1
  echo "[guest_desktop_link] $REAL_LINK (no x86_64-elf-ld) -o $OUT ..." >&2
  mmap_early=()
  [[ -f "$GUEST_MMAP" ]] && mmap_early+=("$GUEST_MMAP")
  exec "$REAL_LINK" -nostdlib -nodefaultlibs \
    -Wl,-e,_start -Wl,--no-gc-sections \
    -T "$SCRIPT" \
    -o "$OUT" \
    "$CRT0" "$COMPAT" "$GUEST_SERIAL" \
    "${mmap_early[@]}" \
    "${objs[@]}" \
    -Wl,--start-group "${archives_grouped[@]}" "$libc_link" ${libm_a:+"$libm_a"} "$libstdcxx_a" -Wl,--end-group \
    "$libgcc_a" \
    -Wl,--wrap=getenv -Wl,--wrap=getuid -Wl,--wrap=geteuid \
    -Wl,--wrap=setlocale -Wl,--wrap=nl_langinfo -Wl,--wrap=getcwd -Wl,--wrap=readlink \
    -Wl,--wrap=gettimeofday -Wl,--wrap=clock_gettime -Wl,--wrap=nanosleep -Wl,--wrap=stat -Wl,--wrap=lstat \
    -Wl,--wrap=open -Wl,--wrap=opendir -Wl,--wrap=realpath -Wl,--wrap=abort \
    -Wl,--wrap=poll -Wl,--wrap=ppoll -Wl,--wrap=epoll_wait -Wl,--wrap=sched_yield -Wl,--wrap=pthread_cond_wait -Wl,--wrap=pthread_cond_timedwait \
    -Wl,--wrap=pthread_create -Wl,--wrap=pthread_once -Wl,--wrap=pthread_self \
    -Wl,--wrap=pthread_key_create -Wl,--wrap=pthread_key_delete \
    -Wl,--wrap=pthread_setspecific -Wl,--wrap=pthread_getspecific \
    -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pthread_mutex_unlock \
    -Wl,--wrap=pthread_mutex_init -Wl,--wrap=pthread_cond_init \
    -Wl,--wrap=_Unwind_RaiseException \
    -Wl,--wrap=_Z21qRegisterResourceDataiPKhS0_S0_
fi

[[ -f "$OUT" ]] || {
  echo "guest_desktop_link: link finished but missing output: $OUT" >&2
  exit 1
}
sz=$(stat -c%s "$OUT" 2>/dev/null || wc -c <"$OUT")
echo "[guest_desktop_link] wrote $OUT ($sz bytes)" >&2
