#!/usr/bin/env bash
# Build static guest regression probes for M13 QEMU ring-3 tests.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build/host-tests"
REGRESS="${ROOT}/tools/guest_regress"
LD="${REGRESS}/guest_regress.ld"
CFLAGS="-nostdlib -static -fno-pie -fno-stack-protector"

mkdir -p "${BUILD}"

build_probe() {
	local src="$1"
	local out="$2"

	echo "== build_guest_regress: ${out} =="
	gcc ${CFLAGS} -Wl,-T,"${LD}" -Wl,-e,_start \
		-o "${BUILD}/${out}" "${REGRESS}/${src}"
	file -b "${BUILD}/${out}"
}

build_probe ltp_open_guest.S ltp_open_guest.elf
build_probe posix_io_guest.S posix_io_guest.elf

echo "OK: guest regress probes in ${BUILD}"
