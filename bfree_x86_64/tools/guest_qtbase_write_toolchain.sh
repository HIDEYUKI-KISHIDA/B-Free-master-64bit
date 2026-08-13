#!/usr/bin/env bash
# Shared guest qtbase cross toolchain (musl sysroot, block host /usr/include).
# shellcheck shell=bash
guest_qtbase_write_toolchain_cmake() {
  local out="$1" musl="$2" libgcc_dir="$3" elf_root="$4" cxx_inc="$5" cxx_target="$6"
  local extra_root="${7:-}"
  local cxx_isystem="" gcc_intrinsic="" gcc_fixed=""
  local posix_defs="-DPATH_MAX=4096 -DNAME_MAX=255 -D_POSIX_PIPE_BUF=512 -DPIPE_BUF=4096"
  if [[ -d "${libgcc_dir}/include" ]]; then
    gcc_intrinsic+=" -isystem ${libgcc_dir}/include"
  fi
  if [[ -d "${libgcc_dir}/include-fixed" ]]; then
    gcc_fixed+=" -isystem ${libgcc_dir}/include-fixed"
  fi
  if [[ -n "$cxx_inc" ]]; then
    cxx_isystem+=" -isystem ${cxx_inc}"
    if [[ -n "$cxx_target" ]]; then
      cxx_isystem+=" -isystem ${cxx_target}"
    fi
  fi
  cat >"$out" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSROOT "${musl}")
set(CMAKE_C_COMPILER x86_64-elf-gcc)
set(CMAKE_CXX_COMPILER x86_64-elf-g++)
set(CMAKE_AR x86_64-elf-ar)
set(CMAKE_RANLIB x86_64-elf-ranlib)
set(CMAKE_STRIP x86_64-elf-strip)
set(CMAKE_FIND_ROOT_PATH "$elf_root;${musl}${extra_root}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_LIBRARY_PATH "$libgcc_dir;${musl}/lib")
set(CMAKE_INCLUDE_PATH "${musl}/include")
# -nostdinc/-nostdinc++: block host /usr/include (cstdint -> stdint.h -> bits/libc-header-start.h).
# CXX needs BOTH: -nostdinc++ alone still pulls glibc stdint.h via libstdc++ <cstdint>.
# libgcc include: x86intrin.h. include-fixed AFTER musl so limits.h comes from musl.
# posix_defs: limits.h macros when headers are not explicitly included.
set(CMAKE_C_FLAGS "-nostdinc -isystem ${musl}/include${gcc_intrinsic}${gcc_fixed} -D__linux__ -D_GNU_SOURCE ${posix_defs} -L${musl}/lib -L${libgcc_dir}")
set(CMAKE_CXX_FLAGS "-nostdinc -nostdinc++ -D__linux__ -D_GNU_SOURCE ${posix_defs} -L${musl}/lib -L${libgcc_dir} -isystem ${musl}/include${gcc_intrinsic}${cxx_isystem}${gcc_fixed}")
set(CMAKE_EXE_LINKER_FLAGS "-L${libgcc_dir} -L${musl}/lib")
EOF
}
