#!/usr/bin/env bash
# Guest ash regression (requires booted guest + BusyBox rootfs).
# Host M3 gate is test_p4_shell_cli via phase3_guest_auto.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROOTFS="${ROOT}/guest/rootfs"

if [[ ! -x "${ROOTFS}/bin/busybox" ]]; then
  echo "SKIP: no guest rootfs at ${ROOTFS}/bin/busybox" >&2
  echo "Run tools/build_guest_busybox.sh after vendoring BusyBox." >&2
  exit 0
fi

echo "phase3_guest_ash: stub — run ash scripts from tools/ash_regress/ on guest" >&2
exit 0
