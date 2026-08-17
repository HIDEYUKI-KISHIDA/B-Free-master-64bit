#!/usr/bin/env bash
# Phase 0 helper: fetch official T-Kernel 2.0 as a reference tree.
# Official tkernel_2 is not AArch64; STOS bring-up lives in kernel/bringup.
set -euo pipefail

DEST="${1:-$(cd "$(dirname "$0")/.." && pwd)/third_party/tkernel_2}"
mkdir -p "$(dirname "$DEST")"
if [[ -d "$DEST/.git" ]]; then
  echo "already present: $DEST"
  exit 0
fi
git clone --depth 1 https://github.com/tron-forum/tkernel_2.git "$DEST"
echo "cloned T-Kernel 2.0 (T-License 2.2) → $DEST"
