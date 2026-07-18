#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
sed -i 's/\r$//' tools/fix_v245_loadimplicit_guest.py tools/v246_rebuild.sh
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
t = p.read_text()
old = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_implicit_skip");
    m_importCache->setBaseUrl(QUrl(), bfree_guest_static_qml_url());
    return true;
#else"""
new = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_implicit_skip");
    return true;
#else"""
if new in t:
    print("[v246] implicit skip already without setBaseUrl")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[v246] removed redundant setBaseUrl from loadImplicitImport")
else:
    raise SystemExit("[v246] loadImplicitImport guest block missing")
PY
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/qml/qqmltypedata.cpp.o"
cmake --build "$BD" --target Qml -j12
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
sed -i 's/mmap96-v[0-9]*/mmap96-v246/' userland/desktop_qt/guest_main.cpp
rm -f userland/desktop_qt/desktop.elf
bash tools/build_guest_desktop_elf.sh
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
strings userland/desktop_qt/desktop.elf | grep 'mmap96-v' | head -1
echo "[v246] done"
