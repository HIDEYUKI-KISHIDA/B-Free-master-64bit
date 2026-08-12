# Compositor-first 表示スタック移行

更新: 2026-08-12  
目標: **compositor が VRAM を独占**し、desktop.elf は **Wayland クライアント**（buffer 渡し）のみ。

---

## アーキテクチャ

```
GRUB → kernel.elf (BFREE_BOOT_GUI_FIRST=1)
         → compositor.elf (PID1, BFREE_ROLE_COMPOSITOR)
              → wl_display + wl_compositor + wl_shm → 唯一の FB scanout
         → init.elf (module) → login → exec desktop.elf
              → Qt -platform wayland → wl_surface attach/commit
```

MVP（`-platform bfree` + FB 直書き）は **legacy メニュー**として残す。本線 GRUB default は compositor エントリ。

---

## リポジトリ内の変更（本 PR）

| 変更 | 内容 |
|------|------|
| `kernel/fb_splash.*` | `fb_boot_compositor_handoff()` — カーネルは solid fill のみ |
| `kernel/sysmain/main.c` | `BFREE_BOOT_GUI_FIRST` 時スプラッシュ/logo 停止 |
| `kernel/Makefile` | `make compositor-kernel` ターゲット |
| `guest_main.cpp` | `BFREE_GUEST_WAYLAND_CLIENT` → `-platform wayland`、bfree QPA 未登録 |
| `boot/grub/grub.cfg.template` | compositor-first メニュー（committed） |
| `tools/build_compositor_guest_iso.sh` | フル ISO ビルドオーケストレーション |
| `tools/build_compositor_guest_elf.sh` | gui_server → compositor.elf（x86_64-elf クロス） |
| `tools/build_guest_qtwayland.sh` | guest Qt に QtWayland 追加 |
| `tools/build_guest_desktop_wayland_elf.sh` | Wayland client desktop.elf |

---

## 開発マシンで必要（git 外）

正本 WSL ツリーに存在するが、本リモートには未コミットのもの:

- `gui_server/` — compositor.elf ビルド
- `userland/init/` — PID1 ログイン → desktop exec
- `build.sh` — ISO 組立
- guest Qt prefix + `tools/build_bfree_qt6_guest.sh` 成果物

---

## WSL ビルド手順

```bash
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
git pull origin cursor/compositor-wayland-5a9f

export PATH="$HOME/x86_64-elf-toolchain/bin:$PATH"
export BFREE_QT_GUEST_BUILD_DIR=/home/h_kis/out/bfree-qt6-guest-static
export BFREE_QT_SRC=/home/h_kis/src/qt6   # qtwayland ソース

# 1) guest QtWayland（初回のみ、数時間）
bash tools/build_guest_qtwayland.sh

# 2) compositor-first ISO
bash tools/build_compositor_guest_iso.sh

# 3) QEMU
bash tools/build_compositor_guest_iso.sh run
```

---

## 完了条件（DoD）

- [ ] serial: `[BOOT] Compositor handoff fill done -> ring3 compositor.`
- [ ] serial: `[desktop_qt] platform=wayland (compositor client)`
- [ ] serial に `[QPA] update` が出ない（direct FB 書き込みなし）
- [ ] VGA: DesktopShell が単一レイヤーで表示（タイル化・3 重レイヤーなし）
- [ ] `tools/run_wayland_full_regression.sh`（HOST）が引き続き PASS

---

## 入力ポリシー

`BFREE_WAYLAND_INPUT_STRICT=1`（`make compositor-kernel` / RELEASE）では、  
`sys_poll_input_event` は **compositor ロールのみ**生入力可。desktop.elf は Wayland seat 経由。

---

## ロールバック

GRUB メニュー 2: **legacy fbdev QPA**（従来 init → desktop `-platform bfree`）。
