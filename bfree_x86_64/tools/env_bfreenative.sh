#!/usr/bin/env bash
#   sed -i 's/\r$//' tools/env_bfreenative.sh tools/bfree_resolve_toolchain.sh
#   source tools/env_bfreenative.sh

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=bfree_resolve_toolchain.sh
. "$ROOT/tools/bfree_resolve_toolchain.sh"
bfree_apply_toolchain_env || return 1 2>/dev/null || exit 1

export BFREE_ROOT="$ROOT"
export BFREE_QT_GUEST_BUILD_DIR="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
export BFREE_QT_BUILD_DIR="${BFREE_QT_BUILD_DIR:-/root/out/bfree-qt6-static}"
