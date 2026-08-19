# Native OS / 本デスク TODOLIST

4 つの言葉は **別物**。混ぜない。

| 言葉 | 意味 | 今 |
|------|------|-----|
| **Native OS** | ゲストで動く OS 全体（カーネル + POSIX + 何かの机） | 起動できる。日次机あり |
| **Wayland** | compositor がソケットを持ち、クライアントが窓を出す **手順** | W8 済（金窓）。W9 はまだ |
| **本デスク** | 製品の机。compositor が画素を持ち、`DesktopShell.qml` が Qt Wayland クライアント | D1 済。D2 第一段済。D2b は engine ctor で停止（`BFREE_D2B_QML=0` で第一段に戻した）。D3 はまだ |
| **GPU** | 絵を GPU で出す | まだ。今は全部ソフトウェア FB |

**D1 済。D2 第一段済。D2b は engine ctor で停止（第一段に戻した）。** 日次 `bfree.iso` は上書きしない。GPU も触らない。

正本はこのファイル。`HONDESK_PHASES.ja.md` は前段 S0–S2 の記録。`AGENTS.md` と同期する。

スクリプトは `bfree_x86_64/` から。日次 `bfree.iso` と `kernel.elf.g1-desk` は上書きしない。

---

## A. Native OS（OS 全体）

Wayland でも GPU でも本デスクでもない。

- [x] **N1** `g1-desk` カーネルで QEMU 起動
- [x] **N2** 日次机 `bfree.iso`（`QT_QPA_PLATFORM=bfree` が FB を直接塗る。EX / VW / TE）
- [x] **N3** persist（`-drive file=persist.img,...`。EX が `/persist/desk.txt` = `from-desk`）

日次机は **仮の製品見た目**。本デスクに置き換えるのは D1 以降。

```
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
qemu-system-x86_64 -cdrom bfree.iso -m 1024 -vga std -serial file:/tmp/bfree_serial.log
```

---

## B. Wayland（プロトコル。机の中身ではない）

合意 step 1–3。stub ISO だけ。上流 qtwayland ではない。

- [x] **W1** S0 compositor が FB を塗る
- [x] **W2** 同一プロセスの wire + shm
- [x] **W3** `vfork`(58) で 2 プロセス（`fork`(57) 禁止）
- [x] **W4** AF_UNIX `/tmp/wayland-0`
- [x] **W5** ゲスト矢印カーソル
- [x] **W6** step 1: xdg-shell で窓（`[compositor] xdg-shell window`）
- [x] **W7** step 2: `desktop.elf` ではない C クライアント `p8test.elf`（緑 `Qt` / `wayland` / バラ `shm`）
- [x] **W8** step 3: 本物 `QGuiApplication` が stub QPA で金/紺/シアンを塗って **return 0**
  - 済: `ctor mmap ok` / factory create / `ctor ok` / `fill bits` / `flush` / `exit_group`
  - 済: `[qt] hello wait-stub` → `[VFORK] parent resume` → `[wl] vfork parent`
  - 済: 画面は金バー `0xD4A017` / 紺 `0x1E3A8A` / シアン印 `0x06B6D4`（C の緑タイトルではない）。周りの EX/VW/TE は stub 仮 chrome
- [ ] **W9** 本物 qtwayland（`wl_seat` / `SCM_RIGHTS`）。W8 のあと。今やらない

W8 の完了条件:

- シリアル: `hello hybrid-qpa` → `QPA factory create` → `ctor ok` → `QGuiApplication done` → `[wl] vfork parent`
- 画面: 金 `0xD4A017` / 紺 `0x1E3A8A` / シアン `0x06B6D4`（C の緑ではない）
- hello は **return 0**。`mmap noreturn` 禁止。`processEvents` は初回フレームでは回さない

C デスクは **W8 の手順ではない**。Qt が灰色のとき、動く机だけ欲しい退避。

---

## C. 本デスク（製品の机。Wayland のあと）

日次 N2 の机をこれに差し替える。W8 金窓・D1 wayland 済。**D2 第一段済。D2b は engine ctor で停止（第一段に戻した）。** 日次 ISO は触らない。

- [x] **D1** step 4: stub の Qt クライアントが bfree QPA を描画に選ばない。`getenv QT_QPA_PLATFORM=wayland` → `exit_group` → `[wl] vfork parent`。日次 `bfree.iso` は **上書きしていない**（N2 の EX/TE は残す）。`desktop_qt/guest_link_compat.o` は上書きしていない。g1-desk の `desktop.elf` 注入は触っていない
- [ ] **D2** `DesktopShell.qml` を **Wayland クライアント**として載せる（boot の `beginCreate` は死んでいる。やり直さない）
  - [x] 第一段: hello が `userland/compositor_stub/DesktopShell.qml` を運び、GuestMvpShell レイアウトを QImage bits で塗る。シリアル `D2 qml-client` → `hello wait-stub` → `getenv QT_QPA_PLATFORM=wayland` → `D2 argv -platform wayland` → `D2 fill desk` → `exit_group`（`ppid=1`）→ `[VFORK] parent resume` → `[wl] vfork parent`。親シリアルは `exit_group` の数秒後でもよい（~35MB hello）。窓は 480×320。壁紙 `#7A8FA8` / 白カード / バー `#334155` / EX `#1D4ED8` / VW `#0F766E` / TE `#C2410C`。周りの EX/VW/TE は stub 仮 chrome
  - [ ] **D2b** `QQmlEngine` を Wayland クライアントで作る。観測: `D2b qml-engine` → `D2b engine enter` → `D2b engine operator new ok` でコンストラクタが戻らない。退避済: `BFREE_D2B_QML=0`（第一段 `fill desk` → `exit_group` → `[wl] vfork parent`）。`beginCreate` / `loadUrl` はしない。engine ctor は合意するまで再試行しない
  - [ ] まだ: 製品 QML の qmlcache `populate`（IR Ready）。`beginCreate` は使わない
- [ ] **D3** 起動できるカーネルで `desktop.elf` を Wayland exec（旧 S2）。from-source kernel を stub ISO に載せない

仮 chrome / 偽物 Explorer は製品にしない。GTK にしない。

---

## D. GPU（最後。Wayland でも本デスクでもない）

ソフトウェア FB blit のまま。W8 も D2 も GPU を要求しない。

- [ ] **G1** DRM / KMS
- [ ] **G2** EGL / Qt Rhi
- [ ] **G3** scene graph を GPU に載せる

---

## 今やらない

- compositor `fork`(57)
- stub ISO に自前 `kernel.elf`
- `BFREE_BOOT_GUI_FIRST=1` を日次へ
- g1-desk で `execve("desktop.elf")`
- Gate 1 / `beginCreate` のやり直し
- W9 / D* / G* を W8 より先にやる
