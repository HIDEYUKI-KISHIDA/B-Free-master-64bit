#!/usr/bin/env bash
# Build minimal newc cpio for M8 kernel embed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 "${ROOT}/tools/build_initramfs.py"
