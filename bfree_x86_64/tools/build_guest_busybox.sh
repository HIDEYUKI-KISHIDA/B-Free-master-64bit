#!/usr/bin/env bash
# Build guest BusyBox rootfs (upstream, no B-Free shell hacks).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THIRD="${ROOT}/third_party"
VERSION_FILE="${THIRD}/BUSYBOX_VERSION"
BUSYBOX_SRC="${BUSYBOX_SRC:-${THIRD}/busybox}"
ROOTFS="${ROOT}/guest/rootfs"
CONFIG="${BUSYBOX_CONFIG:-${ROOT}/configs/busybox_posix.config}"
DOC="${ROOT}/../docs/M3_BUSYBOX_PATCH_ROLLBACK.md"
URL_BASE="https://busybox.net/downloads"

echo "== build_guest_busybox: guest rootfs (config=$(basename "${CONFIG}")) =="

if [[ ! -f "${VERSION_FILE}" ]]; then
  echo "ERROR: missing ${VERSION_FILE}" >&2
  exit 1
fi
BB_VER="$(tr -d '[:space:]' < "${VERSION_FILE}")"

fetch_busybox() {
  local tarball="${THIRD}/busybox-${BB_VER}.tar.bz2"
  if [[ -d "${BUSYBOX_SRC}" ]]; then
    return 0
  fi
  echo "Fetching BusyBox ${BB_VER}..." >&2
  mkdir -p "${THIRD}"
  if [[ ! -f "${tarball}" ]]; then
    wget -q -O "${tarball}" "${URL_BASE}/busybox-${BB_VER}.tar.bz2"
  fi
  tar -xjf "${tarball}" -C "${THIRD}"
  mv "${THIRD}/busybox-${BB_VER}" "${BUSYBOX_SRC}"
}

fetch_busybox

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

if [[ ! -f "${CONFIG}" ]]; then
  echo "ERROR: missing ${CONFIG}" >&2
  exit 1
fi

# Regenerate posix config when using the default expanded profile.
if [[ "${CONFIG}" == "${ROOT}/configs/busybox_posix.config" ]]; then
  python3 "${ROOT}/tools/expand_busybox_config.py"
fi

cp "${CONFIG}" "${BUSYBOX_SRC}/.config"
make -C "${BUSYBOX_SRC}" oldconfig
make -C "${BUSYBOX_SRC}" -j"$(nproc)" busybox

rm -rf "${ROOTFS}"
mkdir -p "${ROOTFS}"
make -C "${BUSYBOX_SRC}" CONFIG_PREFIX="${ROOTFS}" install

if [[ ! -x "${ROOTFS}/bin/busybox" ]]; then
  echo "ERROR: install failed — no ${ROOTFS}/bin/busybox" >&2
  exit 1
fi

APPLET_COUNT="$(find "${ROOTFS}/bin" "${ROOTFS}/usr/bin" -type l 2>/dev/null | wc -l | tr -d ' ')"
echo "OK: ${ROOTFS}/bin/busybox ($(file -b "${ROOTFS}/bin/busybox"))"
echo "OK: ${APPLET_COUNT} applet symlinks under ${ROOTFS}"
echo "OK: no hack markers; upstream ash ready for guest rootfs"
