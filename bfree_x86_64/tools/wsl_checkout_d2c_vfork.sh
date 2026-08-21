#!/usr/bin/env bash
# Switch to cursor/d2c-vfile-slots-9760 safely (stash local WIP first).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/.."
BRANCH="${BFREE_D2C_BRANCH:-cursor/d2c-vfile-slots-9760}"
EXPECT="${BFREE_D2C_EXPECT:-d799e1a}"

echo "[d2c-checkout] repo=$(pwd)"
echo "[d2c-checkout] current=$(git branch --show-current 2>/dev/null || echo '?')"
git fetch origin "$BRANCH"

if [[ -n "$(git status --porcelain)" ]]; then
  echo "[d2c-checkout] stashing all local changes (tracked + untracked)..."
  git stash push -u -m "wip before $BRANCH $(date -Iseconds)"
fi

git checkout -B "$BRANCH" "origin/$BRANCH"
HEAD="$(git rev-parse --short HEAD)"
echo "[d2c-checkout] HEAD=$HEAD (expect prefix $EXPECT)"
if [[ "$HEAD" != "$EXPECT"* ]]; then
  echo "WARN: HEAD is not $EXPECT — fetch may be stale; run: git fetch origin $BRANCH" >&2
fi

echo "[d2c-checkout] OK — now rebuild:"
echo "  cd bfree_x86_64"
echo "  export PATH=\"\$HOME/xshim:\$HOME/x86_64-elf-toolchain/bin:\$PATH\""
echo "  make -C kernel clean && make -C kernel RELEASE=1"
echo "  strings kernel/kernel.elf | grep -E 'build=d2c-vfork-3|\\[VFORK\\] eg'"
echo "  make -C userland/compositor_stub clean all"
echo "  BFREE_D2B_QML=0 bash tools/build_qt_wl_hello.sh"
echo "  strings userland/compositor_stub/qt_wl_hello.elf | grep build=d2c-vfork-3"
echo "  bash tools/_d2c_compositor_stub_smoke.sh"
