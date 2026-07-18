#!/usr/bin/env bash
# One-shot: guest busybox + kernel musl stack + ISO (serial smoke).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/home/h_kis/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

bash tools/build_guest_busybox.sh
export BFREE_GUEST_BUSYBOX=1 BFREE_BOOT_BUSYBOX=1 BFREE_AUTO_LOGIN=1
make -C kernel -j"$(nproc 2>/dev/null || echo 4)"
make -C userland/init clean
make -C userland/init BFREE_AUTO_LOGIN=1 BFREE_BOOT_BUSYBOX=1
bash build.sh
strings userland/busybox_guest/busybox.elf | head -1 || true
echo "[build_busybox_iso] done: $ROOT/bfree.iso"
