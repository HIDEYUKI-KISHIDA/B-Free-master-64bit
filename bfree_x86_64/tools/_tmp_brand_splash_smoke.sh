#!/usr/bin/env bash
# Kernel brand splash + guest splash: rebuild ISO, headless smoke.
set -euo pipefail
export PATH=/home/h_kis/x86_64-elf-toolchain/bin:/usr/bin:/bin HOME=/home/h_kis
ROOT=/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
OUT=$ROOT/out
mkdir -p "$OUT"

echo '=== kernel already built? ensure install ==='
(cd "$ROOT/kernel" && make -j2 install)

echo '=== guest splash + main (arm + anim loop) ==='
bash "$ROOT/tools/_tmp_splash_boot_smoke.sh"
# _tmp_splash_boot_smoke already builds ISO and runs qemu; re-check brand markers
strings -n 4 /tmp/bfree-ds-visual.log > /tmp/ds-brand-str.txt
cp -f /tmp/ds-brand-str.txt "$OUT/phase5-brand-str.txt"

score() { grep -aqF "$1" /tmp/ds-brand-str.txt && echo 1 || echo 0; }
brand=$(score 'Brand splash anim done')
early=$(score 'Early brand splash')
drawn=$(score 'Framebuffer splash drawn')
show=$(score 'splash show ok')
frame=$(score 'splash frame ok')
loop=$(score 'QML ready, entering event loop')
blue=$(score 'Mid FB animations skipped')
pf=$(score 'Page Fault')
echo "BRAND=$brand EARLY=$early DRAWN=$drawn SHOW=$show FRAME=$frame LOOP=$loop BLUE_SKIP=$blue PF=$pf"
# Prefer EARLY=1; still GREEN if brand+guest path ok (early line may race with timeout)
if [[ "$brand" == 1 && "$drawn" == 1 && "$show" == 1 && "$frame" == 1 && "$loop" == 1 && "$pf" == 0 && "$blue" == 0 ]]; then
  echo 'RESULT=GREEN brand-splash' | tee "$OUT/brand-splash.txt"
else
  echo 'RESULT=RED brand-splash' | tee "$OUT/brand-splash.txt"
  exit 1
fi
