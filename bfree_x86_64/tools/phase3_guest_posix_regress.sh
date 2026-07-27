#!/usr/bin/env bash
# POSIX host regression — exercises M1–M5 surface via curated markers.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build/host-tests"
REGRESS="${ROOT}/tools/posix_regress"
FAIL=0

run_case() {
	local script="$1"
	local name
	name="$(basename "${script}")"
	echo "== phase3_guest_posix_regress: ${name} =="
	if ! bash "${script}"; then
		FAIL=1
	fi
}

mkdir -p "${BUILD}"
make -C "${ROOT}" -s host-tests

for script in "${REGRESS}"/*.sh; do
	[[ -f "${script}" ]] || continue
	run_case "${script}"
done

if [[ "${FAIL}" -ne 0 ]]; then
	echo "POSIX_REGRESS: FAIL"
	exit 1
fi

echo "POSIX_REGRESS: ALL PASS"
