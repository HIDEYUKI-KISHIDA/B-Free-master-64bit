#!/usr/bin/env bash
# Restore B-Free guest Qt WSL environment after reinstall / reset.
#
# One command (from repo root, will prompt for sudo password):
#   cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64   # your repo path
#   bash tools/restore_wsl_guest_env.sh --yes
#
# Check only:
#   bash tools/restore_wsl_guest_env.sh --check
#
# Phases: apt deps → elf toolchain → musl → Qt sources → guest Qt (hours) → qmlcache tools
# Optional desktop.elf:  bash tools/restore_wsl_guest_env.sh --yes --with-desktop

if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export BFREE_ROOT="$ROOT"
export BFREE_QT_SRC="${BFREE_QT_SRC:-/root/src/qt6}"
export BFREE_QT_GUEST_BUILD_DIR="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
export BFREE_QT_BUILD_DIR="${BFREE_QT_BUILD_DIR:-/root/out/bfree-qt6-static}"
export BFREE_ELF_LIBM_DIR="${BFREE_ELF_LIBM_DIR:-/root/out/x86_64-elf-libm}"
export BFREE_X86_64_ELF_TOOLS="${BFREE_X86_64_ELF_TOOLS:-/root/x86_64-elf-toolchain}"
export PATH="$BFREE_X86_64_ELF_TOOLS/bin:${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
JOBS="${BFREE_RESTORE_JOBS:-2}"
AUTO_YES=0
CHECK_ONLY=0
SKIP_QT=0
WITH_DESKTOP=0

usage() {
  sed -n '2,20p' "$0"
  echo ""
  echo "Options:"
  echo "  --check          diagnose only"
  echo "  --yes, -y        no confirmation prompts"
  echo "  --skip-qt        skip Qt clone + guest Qt rebuild"
  echo "  --with-desktop   also run build_guest_desktop_elf.sh at end"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --check) CHECK_ONLY=1; shift ;;
    --yes|-y) AUTO_YES=1; shift ;;
    --skip-qt) SKIP_QT=1; shift ;;
    --with-desktop) WITH_DESKTOP=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

log() { echo "[restore] $*"; }
ok()  { echo "[restore] OK: $*"; }
miss(){ echo "[restore] MISSING: $*"; }
run_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  else
    sudo env \
      BFREE_ROOT="$BFREE_ROOT" \
      BFREE_QT_SRC="$BFREE_QT_SRC" \
      BFREE_QT_GUEST_BUILD_DIR="$BFREE_QT_GUEST_BUILD_DIR" \
      BFREE_QT_BUILD_DIR="$BFREE_QT_BUILD_DIR" \
      BFREE_ELF_LIBM_DIR="$BFREE_ELF_LIBM_DIR" \
      BFREE_X86_64_ELF_TOOLS="$BFREE_X86_64_ELF_TOOLS" \
      BFREE_AUTO_CONFIRM=1 \
      BFREE_RESTORE_JOBS="$JOBS" \
      PATH="$PATH" \
      HOME="${HOME:-/root}" \
      "$@"
  fi
}

ensure_root_dirs() {
  run_root mkdir -p /root/src /root/out "$(dirname "$BFREE_X86_64_ELF_TOOLS")"
}

diagnose() {
  log "=== diagnose ==="
  log "repo: $ROOT"
  log "user: $(id -un) uid=$(id -u)"
  local f
  for f in \
    "$BFREE_X86_64_ELF_TOOLS/bin/x86_64-elf-g++" \
    "${HOME}/x86_64-elf-toolchain/bin/x86_64-elf-g++" \
    "$BFREE_ELF_LIBM_DIR/libc.a" \
    "$ROOT/out/x86_64-elf-libm/libc.a" \
    "$BFREE_QT_SRC/qtbase/CMakeLists.txt" \
    "$BFREE_QT_BUILD_DIR/bin/moc" \
    "$BFREE_QT_BUILD_DIR/lib/cmake/Qt6/Qt6Config.cmake" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Core.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Quick.a" \
    "$ROOT/tools/.qt-host-tools/libexec/qmlcachegen" \
    "/usr/lib/qt6/libexec/qmlcachegen" \
    "$ROOT/userland/desktop_qt/desktop.elf"; do
    if [[ -e "$f" ]]; then ok "$f"; else miss "$f"; fi
  done
  log "=== end diagnose ==="
}

phase_apt() {
  log "=== phase: apt dependencies ==="
  run_root apt-get update -qq
  run_root apt-get install -y \
    build-essential git wget unzip curl ca-certificates \
    cmake ninja-build perl python3 flex bison gperf pkg-config \
    qt6-declarative-dev-tools \
    linux-libc-dev \
    libfontconfig1-dev libfreetype6-dev libx11-dev libxext-dev libxrender-dev \
    libssl-dev libglib2.0-dev 2>&1 | tail -20
  ok "apt packages"
}

phase_toolchain() {
  log "=== phase: x86_64-elf g++ toolchain ==="
  if [[ -x "$BFREE_X86_64_ELF_TOOLS/bin/x86_64-elf-g++" ]]; then
    ok "toolchain already at $BFREE_X86_64_ELF_TOOLS"
    return 0
  fi
  if [[ "$(id -u)" -ne 0 ]]; then
    run_root bash "$ROOT/tools/install_x86_64_elf_gpp_prebuilt.sh"
  else
    bash "$ROOT/tools/install_x86_64_elf_gpp_prebuilt.sh"
  fi
  export PATH="$BFREE_X86_64_ELF_TOOLS/bin:$PATH"
  command -v x86_64-elf-g++ >/dev/null
  ok "x86_64-elf-g++ $(x86_64-elf-g++ --version | head -1)"
}

phase_musl() {
  log "=== phase: musl libc for guest link ==="
  if [[ -f "$BFREE_ELF_LIBM_DIR/libc.a" && -f "$BFREE_ELF_LIBM_DIR/libm.a" ]]; then
    ok "musl already at $BFREE_ELF_LIBM_DIR"
    return 0
  fi
  if [[ -f "$ROOT/out/x86_64-elf-libm/libc.a" && -f "$ROOT/out/x86_64-elf-libm/libm.a" ]]; then
    log "copying musl from repo cache → $BFREE_ELF_LIBM_DIR"
    run_root mkdir -p "$BFREE_ELF_LIBM_DIR"
    run_root cp -a "$ROOT/out/x86_64-elf-libm/." "$BFREE_ELF_LIBM_DIR/"
    ok "musl copied from $ROOT/out/x86_64-elf-libm"
    return 0
  fi
  run_root env BFREE_ELF_LIBM_DIR="$BFREE_ELF_LIBM_DIR" \
    BFREE_X86_64_ELF_TOOLS="$BFREE_X86_64_ELF_TOOLS" \
    PATH="$PATH" \
    bash "$ROOT/tools/build_x86_64_elf_libm.sh"
  ok "musl libc"
}

phase_libstdcxx() {
  log "=== phase: x86_64-elf libstdc++.a (required for guest Qt cmake) ==="
  export PATH="$BFREE_X86_64_ELF_TOOLS/bin:$PATH"
  local gxx libstdc libgcc dir
  gxx="$(command -v x86_64-elf-g++)"
  libstdc="$("$gxx" -print-file-name=libstdc++.a 2>/dev/null || true)"
  if [[ -n "$libstdc" && -f "$libstdc" ]]; then
    ok "libstdc++: $libstdc"
    return 0
  fi
  libgcc="$("$gxx" -print-file-name=libgcc.a 2>/dev/null || true)"
  dir="$(dirname "$libgcc")"
  if [[ -f "$dir/libstdc++.a" ]]; then
    ok "libstdc++: $dir/libstdc++.a"
    return 0
  fi
  log "building libstdc++ under /root/bfree-native-build (1–2 hours) ..."
  env \
    BFREE_LINUX_BUILD_ROOT=/root/bfree-native-build \
    BFREE_ELF_MUSL_SYSROOT="$BFREE_ELF_LIBM_DIR/prefix" \
    BFREE_ELF_LIBM_DIR="$BFREE_ELF_LIBM_DIR" \
    BFREE_X86_64_ELF_TOOLS="$BFREE_X86_64_ELF_TOOLS" \
    PATH="$PATH" \
    bash "$ROOT/tools/build_x86_64_elf_libstdcxx.sh"
  env PATH="$PATH" bash "$ROOT/tools/install_x86_64_elf_libstdcxx.sh"
  libstdc="$("$gxx" -print-file-name=libstdc++.a 2>/dev/null || true)"
  [[ -n "$libstdc" && -f "$libstdc" ]] || [[ -f "$dir/libstdc++.a" ]] || {
    log "libstdc++ still missing after build"
    exit 1
  }
  ok "libstdc++"
}

phase_qt_src() {
  log "=== phase: Qt 6.8 sources → $BFREE_QT_SRC ==="
  if [[ -f "$BFREE_QT_SRC/qtbase/CMakeLists.txt" ]]; then
    ok "Qt sources already present"
    return 0
  fi
  run_root env BFREE_QT_SRC="$BFREE_QT_SRC" bash "$ROOT/tools/clone_qt6_src.sh"
  ok "Qt sources"
}

phase_qt_host() {
  log "=== phase: host Qt (QT_HOST_PATH for guest cross-compile) ==="
  if [[ -x "$BFREE_QT_BUILD_DIR/bin/moc" && -r "$BFREE_QT_BUILD_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    ok "host Qt already at $BFREE_QT_BUILD_DIR"
    return 0
  fi
  run_root env \
    BFREE_QT_SRC="$BFREE_QT_SRC" \
    BFREE_QT_BUILD_DIR="$BFREE_QT_BUILD_DIR" \
    JOBS="$JOBS" \
    PATH="$PATH" \
    bash "$ROOT/tools/build_host_qt_minimal.sh"
  ok "host Qt"
}

phase_qt_guest() {
  log "=== phase: guest Qt static (qtbase + qtshadertools + qtdeclarative) ==="
  log "WARNING: this phase takes several hours on WSL (jobs=$JOBS)"
  if [[ -f "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Core.a" && -f "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Quick.a" ]]; then
    ok "guest Qt already built"
    return 0
  fi
  run_root env \
    BFREE_QT_SRC="$BFREE_QT_SRC" \
    BFREE_QT_GUEST_BUILD_DIR="$BFREE_QT_GUEST_BUILD_DIR" \
    BFREE_QT_BUILD_DIR="$BFREE_QT_BUILD_DIR" \
    BFREE_AUTO_CONFIRM=1 \
    JOBS="$JOBS" \
    PATH="$PATH" \
    bash "$ROOT/tools/rebuild_guest_qt_minimal.sh"
  ok "guest Qt"
}

phase_qmlcache_tools() {
  log "=== phase: qmlcachegen for guest MVP precompile ==="
  python3 "$ROOT/tools/write_qmlcache_shell_scripts.py"
  bash "$ROOT/tools/build_host_qmlcachegen.sh"
  bash "$ROOT/tools/gen_guest_mvp_qmlcache.sh" || {
    log "qmlcache gen skipped/failed (guest Qt tools path may differ); continue"
  }
  ok "qmlcache tools"
}

phase_desktop_elf() {
  log "=== phase: guest desktop.elf ==="
  export BFREE_QT_GUEST_BUILD_DIR PATH BFREE_ELF_LIBM_DIR BFREE_X86_64_ELF_TOOLS
  bash "$ROOT/tools/build_guest_desktop_elf.sh"
  ok "desktop.elf"
}

print_env_snippet() {
  cat <<EOF

=== add to ~/.bashrc (optional) ===
export BFREE_QT_SRC=$BFREE_QT_SRC
export BFREE_QT_GUEST_BUILD_DIR=$BFREE_QT_GUEST_BUILD_DIR
export BFREE_QT_BUILD_DIR=$BFREE_QT_BUILD_DIR
export BFREE_X86_64_ELF_TOOLS=$BFREE_X86_64_ELF_TOOLS
export BFREE_ELF_LIBM_DIR=$BFREE_ELF_LIBM_DIR
export PATH="\$BFREE_X86_64_ELF_TOOLS/bin:\$PATH"

=== next (QEMU smoke) ===
cd $ROOT
source tools/env_bfreenative.sh 2>/dev/null || true
bash build_mvp_native_desktop.sh run
grep -E 'mmap96-v|QML ready|EXCEPTION' /tmp/bfree.serial | tail -30

EOF
}

main() {
  cd "$ROOT"
  diagnose
  [[ "$CHECK_ONLY" -eq 1 ]] && exit 0

  if [[ "$AUTO_YES" -ne 1 ]]; then
    echo ""
    echo "Restore will install apt packages, elf toolchain, musl, clone Qt (~GB),"
    echo "and rebuild guest Qt under $BFREE_QT_GUEST_BUILD_DIR (hours)."
    read -r -p "Continue? [y/N] " ans
    case "$ans" in y|Y|yes|YES) ;; *) echo "Aborted."; exit 0 ;; esac
  fi

  ensure_root_dirs
  phase_apt
  phase_toolchain
  phase_musl
  phase_libstdcxx

  if [[ "$SKIP_QT" -eq 0 ]]; then
    phase_qt_src
    phase_qt_host
    phase_qt_guest
  else
    log "skipping Qt clone/build (--skip-qt)"
  fi

  phase_qmlcache_tools

  if [[ "$WITH_DESKTOP" -eq 1 ]]; then
    phase_desktop_elf
  fi

  diagnose
  log "restore finished"
  print_env_snippet
}

main "$@"
