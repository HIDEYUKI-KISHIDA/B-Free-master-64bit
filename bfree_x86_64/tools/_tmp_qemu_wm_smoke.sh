#!/usr/bin/env bash
# QEMU+QMP smoke only (reuse current desktop.elf / kernel / init).
set -euo pipefail
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT=/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
DESK=$ROOT/userland/desktop_qt
ISO_STAGE=/tmp/bfree-stage-abcd-iso
ISO=/tmp/bfree-stage-abcd.iso
QLOG=/tmp/bfree-stage-abcd.log
QMP=/tmp/bfree-stage-abcd.qmp
FAT=/tmp/bfree-stage-abcd-fat.img
[[ -f "$FAT" ]] || bash "$ROOT/tools/_f1_persist_fat_img.sh" "$FAT" "hello-from-fat"
rm -rf "$ISO_STAGE" "$QMP"
rm -f "$ISO"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 "$ROOT/kernel/kernel.elf" "$ISO_STAGE/boot/kernel.elf"
install -m 0644 "$ROOT/userland/init/init.elf" "$ISO_STAGE/boot/initrd.img"
install -m 0644 "$DESK/desktop.elf" "$ISO_STAGE/boot/desktop.elf"
cp -f "$ROOT/iso_root/boot/grub/grub.cfg" "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >/tmp/mkstage.log 2>&1
: > "$QLOG"
(
  for i in $(seq 1 180); do
    if grep -aqF 'entering event loop' "$QLOG" 2>/dev/null; then
      sleep 3
      if [[ -S "$QMP" ]]; then
        python3 - <<'PY' | timeout 55 nc -U "$QMP" >/tmp/qmp-stage.out 2>&1 || true
import json,sys,time
def send(obj):
    sys.stdout.write(json.dumps(obj)+"\n"); sys.stdout.flush(); time.sleep(0.05)
def rel(axis, value, n=1, pause=0.04):
    for _ in range(n):
        send({"execute":"input-send-event","arguments":{"events":[{"type":"rel","data":{"axis":axis,"value":value}}]}})
        time.sleep(pause)
def key(qcode):
    send({"execute":"input-send-event","arguments":{"events":[{"type":"key","data":{"down":True,"key":{"type":"qcode","data":qcode}}}]}})
    time.sleep(0.15)
    send({"execute":"input-send-event","arguments":{"events":[{"type":"key","data":{"down":False,"key":{"type":"qcode","data":qcode}}}]}})
    time.sleep(0.35)
send({"execute":"qmp_capabilities"})
time.sleep(2.0)
key("1")
time.sleep(0.5)
rel("x", 10, 12)
rel("y", 2, 3)
time.sleep(0.2)
send({"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":True}}]}})
time.sleep(0.3)
rel("x", 10, 8)
rel("y", 10, 8)
time.sleep(0.3)
send({"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":False}}]}})
time.sleep(0.4)
rel("x", -10, 33)
rel("y", -10, 37)
time.sleep(0.2)
send({"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":True}}]}})
time.sleep(0.3)
rel("x", 10, 8)
rel("y", 10, 4)
time.sleep(0.3)
send({"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":False}}]}})
time.sleep(0.4)
key("esc")
key("2")
key("esc")
key("3")
key("esc")
key("s")
time.sleep(0.3)
key("1")
time.sleep(0.3)
key("esc")
time.sleep(0.5)
send({"execute":"screendump","arguments":{"filename":"/tmp/bfree-stage-abcd.ppm"}})
time.sleep(0.3)
send({"execute":"quit"})
PY
      fi
      break
    fi
    if grep -aqiE 'Page Fault|PANIC' "$QLOG" 2>/dev/null; then break; fi
    sleep 1
  done
) &
WATCH=$!
timeout 220 qemu-system-x86_64 -m 1024M -no-reboot -boot d -cdrom "$ISO" -vga std -display none \
  -drive "file=${FAT},if=ide,index=0,media=disk,format=raw" \
  -qmp "unix:${QMP},server,nowait" -serial mon:stdio >>"$QLOG" 2>&1 || true
wait "$WATCH" 2>/dev/null || true
echo '=== KEY ==='
fail=0
for t in 'processEvents ok:processEvents' 'W0 mini-WM ready:w0_wm_ready' 'W1 taskbar ready:w1_taskbar' 'W2 window layer ready:w2_window_layer' 'W3 window layer ready:w3_window_layer' 'W3.1 start/taskbar layer ready:w31_start_taskbar' 'W3.2 SG probe ok:w32_sg_probe' 'W3.3 window Quick probe ok:w33_window_probe' 'Start open:start_menu' 'wm drag:wm_drag' 'wm resize:wm_resize'; do
  pat=${t%%:*}; name=${t##*:}
  if grep -aqF "$pat" "$QLOG"; then echo PASS "$name"; else echo FAIL "$name"; fail=1; fi
done
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then echo FAIL panic; fail=1; else echo PASS no_panic; fi
install -m 0644 "$ISO" /home/h_kis/bfree-stage/bfree-stage-abcd.iso
install -m 0644 "$FAT" /home/h_kis/bfree-stage/bfree-stage-abcd-fat.img
install -m 0644 "$DESK/desktop.elf" /home/h_kis/bfree-stage/desktop.elf.latest
install -m 0644 "$QLOG" /home/h_kis/bfree-stage/bfree-stage-abcd.log || true
exit $fail
