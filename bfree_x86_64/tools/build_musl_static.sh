#!/usr/bin/env bash
# Build musl-style static ET_EXEC for P4_MUSL_LOAD / P4_MUSL_EXEC host tests.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THIRD="${ROOT}/third_party"
BUILD="${ROOT}/build/host-tests"
VERSION_FILE="${THIRD}/MUSL_VERSION"
ASM_HELLO="${ROOT}/tools/musl_static/hello.S"
ASM_LD="${ROOT}/tools/musl_static/hello.ld"
C_HELLO="${ROOT}/tools/musl_static/hello.c"
OUT="${BUILD}/musl_static.elf"
TOOLCHAIN="${THIRD}/musl-toolchain"

mkdir -p "${BUILD}"

build_asm_fallback() {
	echo "== build_musl_static: gcc -nostdlib syscall hello =="
	gcc -nostdlib -static -fno-pie -fno-stack-protector \
		-Wl,-T,"${ASM_LD}" -Wl,-e,_start \
		-o "${OUT}" "${ASM_HELLO}"
}

build_musl_toolchain() {
	local ver tarball src

	ver="$(tr -d '[:space:]' < "${VERSION_FILE}")"
	tarball="${THIRD}/musl-${ver}.tar.gz"
	src="${THIRD}/musl-${ver}"

	if [[ -x "${TOOLCHAIN}/bin/musl-gcc" ]]; then
		return 0
	fi

	echo "== build_musl_static: fetching musl ${ver} =="
	mkdir -p "${THIRD}"
	if [[ ! -f "${tarball}" ]]; then
		wget -q -O "${tarball}" "https://musl.libc.org/releases/musl-${ver}.tar.gz"
	fi
	if [[ ! -d "${src}" ]]; then
		tar -xzf "${tarball}" -C "${THIRD}"
	fi

	echo "== build_musl_static: building musl toolchain =="
	if [[ ! -f "${src}/Makefile" ]]; then
		( cd "${src}" && ./configure --prefix="${TOOLCHAIN}" )
	fi
	make -C "${src}" -j"$(nproc)" install
}

build_musl_gcc() {
	if [[ ! -f "${VERSION_FILE}" ]]; then
		build_asm_fallback
		return 0
	fi

	build_musl_toolchain
	if [[ ! -x "${TOOLCHAIN}/bin/musl-gcc" ]]; then
		echo "WARN: musl-gcc missing; using asm fallback" >&2
		build_asm_fallback
		return 0
	fi

	echo "== build_musl_static: musl-gcc static hello =="
	"${TOOLCHAIN}/bin/musl-gcc" -static -no-pie -fno-stack-protector \
		-Wl,-T,"${ASM_LD}" -o "${OUT}" "${C_HELLO}"
}

if [[ "${MUSL_STATIC_ASM_ONLY:-0}" == "1" ]]; then
	build_asm_fallback
else
	build_musl_gcc
fi

file -b "${OUT}"
echo "OK: ${OUT}"
