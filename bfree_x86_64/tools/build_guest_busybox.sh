#!/usr/bin/env bash
# Build guest BusyBox userspace (requires local toolchain + BusyBox tree).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUSYBOX_SRC="${BUSYBOX_SRC:-${ROOT}/third_party/busybox}"
ROOTFS="${ROOT}/guest/rootfs"
DOC="${ROOT}/../docs/M3_BUSYBOX_PATCH_ROLLBACK.md"

echo "== build_guest_busybox: M3 patch-rollback build ==" >&2

if [[ ! -d "${BUSYBOX_SRC}" ]]; then
  echo "SKIP: BusyBox tree not found at ${BUSYBOX_SRC}" >&2
  echo "See ${DOC} for vendoring instructions." >&2
  echo "Host M3 regression: cd ${ROOT} && ./tools/phase3_guest_auto.sh" >&2
  exit 0
fi

# Reject known hack markers (must not be present after M3 rollback).
HACK_MARKERS=(
  "run_pipe_inproc"
  "NOFORK_all"
  "bg_inline"
  "inproc_pipe"
)
for marker in "${HACK_MARKERS[@]}"; do
  if grep -rq "${marker}" "${BUSYBOX_SRC}/shell" 2>/dev/null; then
    echo "ERROR: BusyBox still contains M3 hack marker: ${marker}" >&2
    echo "Remove per ${DOC}" >&2
    exit 1
  fi
done

mkdir -p "${ROOTFS}/bin"
echo "build_guest_busybox: would cross-compile from ${BUSYBOX_SRC}" >&2
echo "build_guest_busybox: install to ${ROOTFS}/bin/busybox" >&2
echo "OK: no hack markers found; integrate cross-compile in local tree." >&2
