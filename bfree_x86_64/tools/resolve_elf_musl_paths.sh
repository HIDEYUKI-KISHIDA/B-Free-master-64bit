#!/usr/bin/env bash
# Resolve BFREE_ELF_LIBM_DIR / BFREE_ELF_MUSL_SYSROOT for cross guest builds.
# Source from other tools: source "$ROOT/tools/resolve_elf_musl_paths.sh"
# shellcheck shell=bash

resolve_elf_musl_libm_dir() {
  local root="${1:-}"
  local candidate

  if [[ -n "${BFREE_ELF_LIBM_DIR:-}" && -f "${BFREE_ELF_LIBM_DIR}/libc.a" ]]; then
    printf '%s\n' "${BFREE_ELF_LIBM_DIR}"
    return 0
  fi

  for candidate in \
    "${HOME}/out/x86_64-elf-libm" \
    "${root}/out/x86_64-elf-libm" \
    "/root/out/x86_64-elf-libm"; do
    [[ -n "$candidate" && -f "$candidate/libc.a" ]] || continue
    printf '%s\n' "$candidate"
    return 0
  done

  printf '%s\n' "${BFREE_ELF_LIBM_DIR:-${HOME}/out/x86_64-elf-libm}"
}

resolve_elf_musl_sysroot() {
  local libm_dir="$1"

  if [[ -n "${BFREE_ELF_MUSL_SYSROOT:-}" && -f "${BFREE_ELF_MUSL_SYSROOT}/include/stdio.h" ]]; then
    printf '%s\n' "${BFREE_ELF_MUSL_SYSROOT}"
    return 0
  fi

  if [[ -f "$libm_dir/prefix/include/stdio.h" ]]; then
    printf '%s\n' "$libm_dir/prefix"
    return 0
  fi

  if [[ -f "$libm_dir/include/stdio.h" ]]; then
    printf '%s\n' "$libm_dir"
    return 0
  fi

  printf '%s\n' "${BFREE_ELF_MUSL_SYSROOT:-$libm_dir/prefix}"
}

resolve_elf_out_prefix() {
  local env_name="$1"
  local default_name="$2"
  local root="${3:-}"
  local val candidate

  val="${!env_name:-}"
  if [[ -n "$val" ]]; then
    printf '%s\n' "$val"
    return 0
  fi

  for candidate in \
    "${HOME}/out/${default_name}" \
    "${root}/out/${default_name}" \
    "/root/out/${default_name}"; do
    [[ -n "$candidate" ]] || continue
    printf '%s\n' "$candidate"
    return 0
  done
}

export_elf_musl_paths() {
  local root="${1:-}"
  BFREE_ELF_LIBM_DIR="$(resolve_elf_musl_libm_dir "$root")"
  BFREE_ELF_MUSL_SYSROOT="$(resolve_elf_musl_sysroot "$BFREE_ELF_LIBM_DIR")"
  export BFREE_ELF_LIBM_DIR BFREE_ELF_MUSL_SYSROOT
}

ensure_x86_64_elf_toolchain_path() {
  local root="${1:-}"
  export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"
  if [[ -n "$root" && -f "$root/tools/ensure_x86_64_elf_toolchain.sh" ]]; then
    BFREE_ROOT="$root" bash <(sed 's/\r$//' "$root/tools/ensure_x86_64_elf_toolchain.sh") || true
  fi
}
