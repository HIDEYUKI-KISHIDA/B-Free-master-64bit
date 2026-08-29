#!/usr/bin/env bash
# Reset local WSL edits blocking D3 branch pull, then sync to origin.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BR="${BFREE_D3_BRANCH:-cursor/d2c-vfile-slots-9760}"

echo "[wsl-sync] repo=$ROOT branch=$BR"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "FAIL: not a git repo: $ROOT" >&2
  exit 1
fi

git fetch origin "$BR"

dirty="$(git status --porcelain -- tools/guest_link_compat.cpp userland/desktop_qt/guest_resource_holder_va.h 2>/dev/null || true)"
if [[ -n "$dirty" ]]; then
  echo "[wsl-sync] clearing local blockers (guest_link_compat.cpp / guest_resource_holder_va.h):"
  echo "$dirty"
  git checkout -- tools/guest_link_compat.cpp 2>/dev/null || true
  rm -f userland/desktop_qt/guest_resource_holder_va.h
fi

if ! git merge-base --is-ancestor HEAD "origin/$BR" 2>/dev/null; then
  git checkout "$BR" 2>/dev/null || git checkout -B "$BR" "origin/$BR"
fi

git pull --ff-only origin "$BR"

need_mark='phdr-text-v1'
if ! grep -qF "$need_mark" tools/guest_link_compat.cpp; then
  echo "FAIL: tools/guest_link_compat.cpp still lacks $need_mark after pull" >&2
  echo "  git log -1 --oneline" >&2
  git log -1 --oneline >&2
  exit 1
fi
if ! grep -qF 'bfree_guest_fill_auxv_core' tools/guest_link_compat.cpp; then
  echo "FAIL: tools/guest_link_compat.cpp missing bfree_guest_fill_auxv_core" >&2
  exit 1
fi

head="$(git rev-parse --short HEAD)"
echo "[wsl-sync] OK HEAD=$head"
if ! grep -qF 'phdr-text-v1' tools/guest_link_compat.cpp; then
  echo "FAIL: after sync, guest_link_compat.cpp still lacks phdr-text-v1" >&2
  exit 1
fi
echo "[wsl-sync] guest_link_compat.cpp has phdr-text-v1 (fixes pthread_once @0x3a7a7d0 on cc59730)"
echo "Next: rm -f userland/desktop_qt/guest_link_compat.o && bash tools/build_desktop_d3_wayland.sh"
echo "      strings userland/desktop_qt/desktop.elf | grep -F 'compat build='"
