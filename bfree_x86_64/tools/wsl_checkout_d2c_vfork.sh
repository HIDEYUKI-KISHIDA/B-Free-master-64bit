#!/usr/bin/env bash
# Switch to cursor/d2c-vfile-slots-9760 safely (stash local WIP first).
# Safe when piped:  cd Program && curl ... | bash
# Or:              bash bfree_x86_64/tools/wsl_checkout_d2c_vfork.sh
set -eu

bfree_find_repo_root() {
  if [[ -n "${BFREE_REPO:-}" && -d "${BFREE_REPO}/.git" ]]; then
    printf '%s\n' "$BFREE_REPO"
    return 0
  fi
  local d="${BFREE_START_DIR:-$PWD}"
  while [[ "$d" != "/" ]]; do
    if [[ -d "$d/.git" ]]; then
      printf '%s\n' "$d"
      return 0
    fi
    d="$(dirname "$d")"
  done
  return 1
}

REPO="$(bfree_find_repo_root)" || {
  echo "FAIL: git リポジトリが見つかりません。" >&2
  echo "  先に cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program してください。" >&2
  exit 1
}
cd "$REPO"

BRANCH="${BFREE_D2C_BRANCH:-cursor/d2c-vfile-slots-9760}"
MIN_COMMIT="${BFREE_D2C_MIN:-dcdd828}"

echo "[d2c-checkout] repo=$REPO"
echo "[d2c-checkout] current=$(git branch --show-current 2>/dev/null || echo '?')"
git fetch origin "$BRANCH"

if [[ -n "$(git status --porcelain)" ]]; then
  echo "[d2c-checkout] stashing all local changes (tracked + untracked)..."
  git stash push -u -m "wip before $BRANCH $(date -Iseconds)"
fi

git checkout -B "$BRANCH" "origin/$BRANCH"
HEAD="$(git rev-parse --short HEAD)"
echo "[d2c-checkout] HEAD=$HEAD on branch=$BRANCH"
if ! git merge-base --is-ancestor "$MIN_COMMIT" HEAD 2>/dev/null; then
  echo "WARN: HEAD is not descended from $MIN_COMMIT — run: git fetch origin $BRANCH" >&2
fi

echo "[d2c-checkout] OK — 次は rebuild:"
echo "  cd bfree_x86_64"
echo "  export PATH=\"\$HOME/xshim:\$HOME/x86_64-elf-toolchain/bin:\$PATH\""
echo "  make -C kernel clean && make -C kernel RELEASE=1"
echo "  strings kernel/kernel.elf | grep -E 'build=d2c-vfork-3|\\[VFORK\\] eg'"
echo "  make -C userland/compositor_stub clean all"
echo "  BFREE_D2B_QML=0 bash tools/build_qt_wl_hello.sh"
echo "  strings userland/compositor_stub/qt_wl_hello.elf | grep build=d2c-vfork-3"
echo "  bash tools/_d2c_compositor_stub_smoke.sh"
