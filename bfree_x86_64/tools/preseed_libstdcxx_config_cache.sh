#!/usr/bin/env bash
# libstdc++ configure is re-run in a fresh dir by GCC make — export cache vars instead.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SITE="$ROOT/tools/libstdcxx-config.site"

export CONFIG_SITE="$SITE"
# shellcheck disable=SC1090
source "$SITE"

echo "[preseed-libstdc++] CONFIG_SITE=$CONFIG_SITE"
echo "[preseed-libstdc++] exported ac_cv_search_shl_load=${ac_cv_search_shl_load:-}"
echo "  Run configure with these in the environment, e.g.:"
echo "    CONFIG_SITE=\"$SITE\" ac_cv_search_shl_load=no make configure-target-libstdc++-v3"
