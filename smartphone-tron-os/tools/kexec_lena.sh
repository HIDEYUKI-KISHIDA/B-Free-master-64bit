#!/usr/bin/env bash
# Spec v0.5.1 §16 — kexec STOS Image on lena (SoMainline already booted).
# Confirm DRAM with: grep -iE 'System RAM|reserved' /proc/iomem
set -euo pipefail

IMAGE="${1:-stos.Image}"
DTB="${2:-lena.dtb}"
MEM_MIN="${MEM_MIN:-0x88000000}"
MEM_MAX="${MEM_MAX:-0x200000000}"

adb push "$IMAGE" /data/local/tmp/stos.Image
adb push "$DTB" /data/local/tmp/lena.dtb

adb shell su -c "
  kexec -l /data/local/tmp/stos.Image \
    --dtb=/data/local/tmp/lena.dtb \
    --command-line='' \
    --mem-min=${MEM_MIN} \
    --mem-max=${MEM_MAX}
  sync
  kexec -e
"
