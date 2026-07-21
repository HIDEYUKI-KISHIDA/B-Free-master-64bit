#!/usr/bin/env bash
set -eu
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
make -C kernel -j"$(nproc 2>/dev/null || echo 4)"
echo "[kernel] build OK"
