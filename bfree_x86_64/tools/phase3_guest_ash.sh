#!/usr/bin/env bash
# Guest ash regression — runs upstream BusyBox ash from guest rootfs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROOTFS="${ROOT}/guest/rootfs"
ASH="${ROOTFS}/bin/busybox"
REGRESS="${ROOT}/tools/ash_regress"
FAIL=0

if [[ ! -x "${ASH}" ]]; then
  echo "== phase3_guest_ash: building rootfs ==" >&2
  "${ROOT}/tools/build_guest_busybox.sh"
fi

if [[ ! -x "${ASH}" ]]; then
  echo "ERROR: no ${ASH}" >&2
  exit 1
fi

export PATH="${ROOTFS}/bin:${ROOTFS}/usr/bin:/bin:/usr/bin"
export BB_ROOT="${ROOTFS}"

run_ash() {
  local script="$1"
  local name
  name="$(basename "${script}")"
  echo "== phase3_guest_ash: ${name} =="
  if ! "${ASH}" ash "${script}"; then
    FAIL=1
  fi
}

for script in "${REGRESS}"/*.sh; do
  [[ -f "${script}" ]] || continue
  run_ash "${script}"
done

if [[ "${FAIL}" -ne 0 ]]; then
  echo "ASH_RESULT: FAIL"
  exit 1
fi

echo "ASH_RESULT: ALL PASS"
