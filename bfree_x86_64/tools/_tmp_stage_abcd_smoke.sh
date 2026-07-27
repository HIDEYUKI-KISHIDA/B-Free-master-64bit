#!/usr/bin/env bash
# Stage A–D: rebuild QPA guest dispatcher/input + desktop.elf + mouse/Explorer smoke.
set -euo pipefail
export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:/usr/bin:/bin"
ROOT=/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
DESK=$ROOT/userland/desktop_qt
FB=/home/h_kis/bfree_build/userland/desktop_qt
QPA=$ROOT/gui_server/integration_gui/bfree_qpa
QPA_BUILD=$QPA/build
QT=/root/out/bfree-qt6-guest-static
INC=/root/bfree-native-build/x86_64-elf-gcc-full/include/c++/13.2.0
MUSL=$ROOT/out/x86_64-elf-libm/prefix/include
CXX=x86_64-elf-g++
export BFREE_ELF_CXX_INCLUDE="$INC"
export BFREE_QT_GUEST_BUILD_DIR=/root/out/bfree-qt6-guest-static
export BFREE_QT_BUILD_DIR=/root/out/bfree-qt6-static
export BFREE_ROOT="$ROOT"

echo "[1] rebuild QPA guestinput (dispatcher + input)"
bash "$ROOT/tools/_tmp_rebuild_qpa_guest.sh"

echo "[2] Item qmlcache + guest_main"
bash "$ROOT/tools/_tmp_gen_item_qmlcache.sh"
cd "$DESK"
CXXFLAGS_COMMON=(
  -c -pipe -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -O2 -std=gnu++1z -fPIC
  -DQT_STATIC -D__linux__ -DBFREE_GUEST_HAS_QT6
  -isystem "$INC" -isystem "$INC/x86_64-pc-elf" -idirafter "$MUSL"
)
$CXX "${CXXFLAGS_COMMON[@]}" -I$QT/include -I$QT/include/QtQml -I$QT/include/QtCore \
  -o guest_desktop_shell_qmlcache.o guest_desktop_shell_qmlcache.cpp
$CXX "${CXXFLAGS_COMMON[@]}" \
  -DQT_QML_LIB -DQT_CORE_LIB -DQT_GUI_LIB -DQT_QUICK_LIB \
  -D_REENTRANT -DQT_NO_DEBUG -DBFREE_GUEST_SKIP_QTQUICK_QRC=1 \
  -I. -I../../gui_server/integration_gui/bfree_qpa \
  -I$QT/include -I$QT/include/QtQml -I$QT/include/QtCore -I$QT/include/QtGui -I$QT/include/QtQuick \
  -I$QT/include/QtQmlIntegration \
  -I$QT/include/QtGui/6.8.0 -I$QT/include/QtGui/6.8.0/QtGui \
  -I$QT/include/QtCore/6.8.0 -I$QT/include/QtCore/6.8.0/QtCore \
  -I$QT/include/QtQuick/6.8.0 -I$QT/include/QtQuick/6.8.0/QtQuick \
  -I$QT/include/QtQml/6.8.0 -I$QT/include/QtQml/6.8.0/QtQml \
  -o guest_main.o guest_main.cpp
$CXX "${CXXFLAGS_COMMON[@]}" -I$QT/include -I$QT/include/QtQml -I$QT/include/QtCore \
  -o guest_mvp_qmlcache_register.o guest_mvp_qmlcache_register.cpp
cp -f "$FB/qrc_guest_desktop.o" "$DESK/qrc_guest_desktop.o" 2>/dev/null || true

/root/out/bfree-qt6-static/bin/qmake6 -o Makefile.guest-elf desktop_qt_guest.pro \
  -qtconf /root/out/bfree-qt6-guest-static/bin/target_qt.conf \
  QMAKE_CC=/root/x86_64-elf-toolchain/bin/x86_64-elf-gcc \
  QMAKE_CXX=/root/x86_64-elf-toolchain/bin/x86_64-elf-g++ \
  QMAKE_LINK="$ROOT/tools/guest_desktop_link_qmake.sh" \
  QT_HOST_PATH=/root/out/bfree-qt6-static \
  QMAKE_QMLIMPORTSCANNER=/root/out/bfree-qt6-static/libexec/qmlimportscanner
inject_obj() {
  local o="$1"
  if ! grep -qE "^[[:space:]]*${o}([[:space:]]|\\\\|$)" Makefile.guest-elf; then
    sed -i "s/^OBJECTS       = guest_main.o/OBJECTS       = ${o} \\\\\n\t\tguest_main.o/" Makefile.guest-elf
    echo "injected $o"
  fi
}
inject_obj guest_qqmltypemodule_add_fix.o
inject_obj guest_qqmltype_create_fix.o
inject_obj guest_qv4_persistent_allocate_fix.o
inject_obj guest_qloggingregistry_fix.o
inject_obj guest_qthreaddata_dtor_fix.o
inject_obj guest_context_factory.o
inject_obj guest_qcoreapp_arguments.o
inject_obj guest_desktop_shell_qmlcache.o
inject_obj guest_qquick_window.o
inject_obj guest_drawhelper_init.o
cp -f "$FB/qrc_guest_desktop.o" "$DESK/qrc_guest_desktop.o"

PRIV_CXXFLAGS=(
  "${CXXFLAGS_COMMON[@]}"
  -DQT_QML_LIB -DQT_CORE_LIB -DQT_GUI_LIB -DQT_QUICK_LIB
  -D_REENTRANT -DQT_NO_DEBUG
  -I. -I$QT/include -I$QT/include/QtQml -I$QT/include/QtCore -I$QT/include/QtGui -I$QT/include/QtQuick
  -I$QT/include/QtQml/6.8.0 -I$QT/include/QtQml/6.8.0/QtQml
  -I$QT/include/QtCore/6.8.0 -I$QT/include/QtCore/6.8.0/QtCore
  -I$QT/include/QtQuick/6.8.0 -I$QT/include/QtQuick/6.8.0/QtQuick
  -I$QT/include/QtGui/6.8.0 -I$QT/include/QtGui/6.8.0/QtGui \
  -I$QT/mkspecs/linux-g++ \
)
$CXX "${PRIV_CXXFLAGS[@]}" -o guest_qqmltype_create_fix.o guest_qqmltype_create_fix.cpp
$CXX "${PRIV_CXXFLAGS[@]}" -o guest_qv4_persistent_allocate_fix.o guest_qv4_persistent_allocate_fix.cpp
$CXX "${PRIV_CXXFLAGS[@]}" -o guest_qloggingregistry_fix.o guest_qloggingregistry_fix.cpp
$CXX "${PRIV_CXXFLAGS[@]}" -o guest_qthreaddata_dtor_fix.o guest_qthreaddata_dtor_fix.cpp
# Always rebuild these — smoke uses make -o and will skip if .o missing.
$CXX "${PRIV_CXXFLAGS[@]}" -o guest_qquick_window.o guest_qquick_window.cpp
$CXX "${PRIV_CXXFLAGS[@]}" -o guest_drawhelper_init.o guest_drawhelper_init.cpp

rm -f desktop desktop.elf
make -f Makefile.guest-elf \
  -o guest_main.o -o guest_mvp_shell_qmlcache.o -o guest_mvp_qmlcache_register.o \
  -o qrc_guest_desktop.o -o guest_qquick_window.o -o guest_drawhelper_init.o \
  -o guest_qml_lookup.o -o desktop_plugin_import.o -o desktop_qml_plugin_import.o \
  -o guest_link_compat.o -o guest_desktop_bridge.o -o guest_bfree_shell_process.o \
  -o moc_guest_desktop_bridge.o -o moc_guest_bfree_shell_process.o \
  -o guest_desktop_shell_qmlcache.o -o guest_platform_stub.o -o qt_futex_guest_stub.o \
  -o guest_qqmltypemodule_add_fix.o -o guest_qqmltype_create_fix.o \
  -o guest_qv4_persistent_allocate_fix.o -o guest_qloggingregistry_fix.o \
  -o guest_qthreaddata_dtor_fix.o \
  -o guest_context_factory.o -o guest_qcoreapp_arguments.o \
  desktop
mv -f desktop desktop.elf
cp -f desktop.elf "$ROOT/iso_root/boot/desktop.elf"
strings desktop.elf | grep -E 'processEvents ok|wsi input pump|Item IR stage1|Item create stage2|registered bfree pointing|Explorer listing|wsi handleMouse' | head -12
echo BUILD_OK

echo "[3] init + FAT persist + ISO + QEMU"
make -C "$ROOT/userland/init" clean >/dev/null
make -C "$ROOT/userland/init" BFREE_AUTO_LOGIN=1 >/dev/null
# Phase A/C: FAT RO /persist with HELLO.TXT for Explorer
FAT_IMG=/tmp/bfree-stage-abcd-fat.img
bash "$ROOT/tools/_f1_persist_fat_img.sh" "$FAT_IMG" "hello-from-fat"
touch "$ROOT/kernel/sysmain/main.c"
make -C "$ROOT/kernel" BFREE_PERSIST_FAT_PROBE=1 -j4 2>&1 | tail -6
ISO_STAGE=/tmp/bfree-stage-abcd-iso
ISO=/tmp/bfree-stage-abcd.iso
QLOG=/tmp/bfree-stage-abcd.log
QMP=/tmp/bfree-stage-abcd.qmp
rm -rf "$ISO_STAGE" "$QMP"
rm -f "$ISO"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 "$ROOT/kernel/kernel.elf" "$ISO_STAGE/boot/kernel.elf"
install -m 0644 "$ROOT/userland/init/init.elf" "$ISO_STAGE/boot/initrd.img"
install -m 0644 "$DESK/desktop.elf" "$ISO_STAGE/boot/desktop.elf"
cp -f "$ROOT/iso_root/boot/grub/grub.cfg" "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
sed -i '/busybox.elf/d;/p8test.elf/d' "$ISO_STAGE/boot/grub/grub.cfg" || true
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
def click():
    send({"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":True}}]}})
    time.sleep(0.5)
    send({"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":False}}]}})
    time.sleep(0.4)
def key(qcode):
    send({"execute":"input-send-event","arguments":{"events":[{"type":"key","data":{"down":True,"key":{"type":"qcode","data":qcode}}}]}})
    time.sleep(0.15)
    send({"execute":"input-send-event","arguments":{"events":[{"type":"key","data":{"down":False,"key":{"type":"qcode","data":qcode}}}]}})
    time.sleep(0.35)
send({"execute":"qmp_capabilities"})
# G0 idle: no forced paint; guest must stay quiet without input.
time.sleep(2.0)
# G1/G2: keys open apps without relying on host cursor alignment.
key("1")
time.sleep(0.5)
# Resize first (window still at 120,80 520x320). SE zone ~(616..640, 376..400).
# From center (512,384) -> (630,390): dx=+118 dy=+6
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
# Drag title: after resize window still near (120,80); title ~(380,100)
# Cursor ends ~ (630+80, 390+80)=(710,470). Move to title.
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
# Start launcher via key 's'
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
  -drive file="$FAT_IMG",if=ide,index=0,media=disk,format=raw \
  -qmp "unix:${QMP},server,nowait" -serial mon:stdio >>"$QLOG" 2>&1 || true
wait "$WATCH" 2>/dev/null || true

QLOG=/tmp/bfree-stage-abcd.log
# Keep a copy for PF analysis
cp -f "$QLOG" /tmp/bfree-sendevent-pf.log 2>/dev/null || true

echo '=== KEY ==='
tr -d '\r' <"$QLOG" | grep -aE 'processEvents|wsi |qt mouse|qt input|qt key|Item IR|Item create|qml root is QQuickItem|QML Item parented|QML Rectangle|component ready|desk open|Start open|Start close|wm drag|wm resize|wm maximize|wm restore|W0 mini-WM|W1 taskbar|W2 window|Page Fault|PANIC|General Protection|registered bfree|Explorer listing|Explorer read|desk note|Viewer|Terminal|SG fail|Phase F|qmlcache HIT|sendEvent|WSI handleMouse|~QThreadData|FAT mounted|input path=' | tail -90
fail=0
grep -aqF 'processEvents ok' "$QLOG" && echo PASS processEvents || { echo FAIL processEvents; fail=1; }
grep -aqF 'wsi input pump armed' "$QLOG" && echo PASS wsi_pump_armed || echo WARN wsi_pump_armed
grep -aqF 'input path=qpa-single' "$QLOG" && echo PASS input_single_path || { echo FAIL input_single_path; fail=1; }
grep -aqF 'W0 mini-WM ready' "$QLOG" && echo PASS w0_wm_ready || { echo FAIL w0_wm_ready; fail=1; }
grep -aqF 'W1 taskbar ready' "$QLOG" && echo PASS w1_taskbar || { echo FAIL w1_taskbar; fail=1; }
grep -aqF 'W2 window layer ready' "$QLOG" && echo PASS w2_window_layer || { echo FAIL w2_window_layer; fail=1; }
grep -aqF 'W3 window layer ready' "$QLOG" && echo PASS w3_window_layer || { echo FAIL w3_window_layer; fail=1; }
grep -aqF 'W3.1 start/taskbar layer ready' "$QLOG" && echo PASS w31_start_taskbar || { echo FAIL w31_start_taskbar; fail=1; }
grep -aqF 'W3.2 SG probe ok' "$QLOG" && echo PASS w32_sg_probe || { echo FAIL w32_sg_probe; fail=1; }
grep -aqF 'W3.3 window Quick probe ok' "$QLOG" && echo PASS w33_window_probe || { echo FAIL w33_window_probe; fail=1; }
grep -aqE 'WSI handleMouse|sendEvent mouse|qt mouse press|mouse bridge-only' "$QLOG" && echo PASS mouse_delivery || echo WARN mouse_delivery
grep -aqE '~QThreadData skip' "$QLOG" && echo PASS qthreaddata_dtor_guard || echo INFO no_qtd_dtor_skip
grep -aqF 'qt mouse press' "$QLOG" && echo PASS qt_mouse_press || echo WARN qt_mouse_press
grep -aqF 'Item IR stage1 ready' "$QLOG" && echo PASS qml_item_stage1 || echo WARN qml_item_stage1
grep -aqE 'Item create stage2 ok|qml root is QQuickItem|GuestDesktopShell QML Item parented' "$QLOG" && echo PASS qml_item_stage2 || echo WARN qml_item_stage2
grep -aqF 'QML Rectangle' "$QLOG" && echo PASS qml_rectangle || echo WARN qml_rectangle
grep -aqF 'SG fail->FB' "$QLOG" && echo PASS qml_sg_fb_fallback || echo WARN qml_sg_fb_fallback
grep -aqF 'component ready' "$QLOG" && echo PASS component_ready || { echo FAIL component_ready; fail=1; }
grep -aqE 'desk open Explorer' "$QLOG" && echo PASS desk_open_explorer || { echo FAIL desk_open; fail=1; }
grep -aqF 'Explorer listing' "$QLOG" && echo PASS stage_d_explorer_listing || { echo FAIL stage_d_listing; fail=1; }
grep -aqF 'Explorer listing persist' "$QLOG" && echo PASS explorer_persist || { echo FAIL explorer_persist; fail=1; }
grep -aqF 'desk note created' "$QLOG" && echo PASS desk_note || { echo FAIL desk_note; fail=1; }
grep -aqE 'from-desk|hello-from-fat' "$QLOG" && echo PASS persist_content || { echo FAIL persist_content; fail=1; }
grep -aqF 'Viewer open' "$QLOG" && echo PASS viewer || { echo FAIL viewer; fail=1; }
grep -aqF 'Terminal ran echo TERM_OK' "$QLOG" && echo PASS terminal || { echo FAIL terminal; fail=1; }
grep -aqF 'Start open' "$QLOG" && echo PASS start_menu || { echo FAIL start_menu; fail=1; }
grep -aqF 'wm drag' "$QLOG" && echo PASS wm_drag || { echo FAIL wm_drag; fail=1; }
grep -aqF 'wm resize' "$QLOG" && echo PASS wm_resize || { echo FAIL wm_resize; fail=1; }
# G0: paint/I/O must not flood serial (once-ish, not every frame).
paint_n=$(grep -acF 'DesktopShell FB painted' "$QLOG" || true)
list_n=$(grep -acF 'Explorer listing persist' "$QLOG" || true)
echo "INFO paint_n=$paint_n list_n=$list_n"
if [[ "${paint_n:-0}" -gt 3 ]]; then echo FAIL serial_paint_flood; fail=1; else echo PASS serial_paint_quiet; fi
if [[ "${list_n:-0}" -gt 8 ]]; then echo FAIL serial_list_flood; fail=1; else echo PASS serial_list_quiet; fi
grep -aqiE 'Page Fault|PANIC|General Protection' "$QLOG" && { echo FAIL panic; fail=1; } || echo PASS no_panic
if [[ "$fail" -ne 0 ]]; then
  bash "$ROOT/tools/_tmp_analyze_sendevent_pf.sh" || true
fi
exit "$fail"
