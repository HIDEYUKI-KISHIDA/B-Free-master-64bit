# 本デスク phases（記録）

チェックボックスの正本は `HONDESK_TODOLIST.ja.md`。
Native OS / Wayland / 本デスク / GPU は **4 トラック**。混ぜない。

このファイルは stub はしご（S0–S2）の記録だけ残す。

---

## 本デスク 4 steps（合意した順番）

TODOLIST では W6 / W7 / W8 / D1+D2。飛ばさない。

これが「本デスクに行く」作業順。飛ばさない。

| Step | 内容 | 状態 | 見た目 / シリアル |
|------|------|------|-------------------|
| **1** | stub xdg-shell。クライアントが窓を作る | **済** | `[compositor] xdg-shell window` |
| **2** | `desktop.elf` ではない小さいクライアント | **済（C）** | `p8test.elf` = C `qt_wl_client.elf`。緑タイトル `Qt` / `wayland` / バラ色 `shm`。`[qt] p8test.elf wayland client` `[wl] vfork parent` `[wl] client shm blit`。`hello.elf` は exec 失敗時の予備 |
| **3** | カーネル + **Qt Wayland**（bfree QPA を選ばない） | **いまここ / vfork parent** | `exit_group` → `[wl] vfork parent` まで緑。証明は金/紺/シアン窓と `[wl] client shm blit` |
| **4** | 日次から bfree QPA を外す。`DesktopShell.qml` を Wayland クライアントとして載せる | **未着手** | 日次 `bfree.iso` を差し替えるのは step 3 が塗れてから。`execve("desktop.elf")` を g1-desk でやらない |

step 3 の中（Qt hello）:

1. `qRegister` を exec スタックでやらない（`desktop.elf` と同じ ctor スタック）
2. `QGuiApplication` を hybrid スタックで作る
3. 1 フレーム描いて **return 0**（g1-desk の `vfork` は子の **exit** 待ち。`mmap noreturn` は使わない）
4. 本物の qtwayland / `wl_seat` / `SCM_RIGHTS` はまだやらない（stub 方言のまま）

---

## stub はしご（step 1–2 まで来た道）

本デスク 4 steps の **前段**。compositor stub ISO（`bfree-compositor-stub.iso`）の中。

| ID | 内容 | 状態 |
|----|------|------|
| **S0** | compositor が FB を塗る（最初はマゼンタ、いま wallpaper 灰 `#7A8FA8`） | **済** |
| **S0b** | 同一プロセスの Wayland 線（`get_registry` … `commit`）+ shm 矩形 | **済** |
| **S0c** | 2 プロセス。`vfork`(Linux **58**) + 子がクライアント | **済**。`fork`(57) は禁止（FB が COW されて灰一色、`[COW] break`） |
| **S0d** | 輸送は AF_UNIX `/tmp/wayland-0`（pipe ではない） | **済** |
| **S0e** | ゲストが矢印カーソルを自分で描く | **済** |
| **S0f** | 仮デスク chrome（アイコン + Start バー）。compositor 側のソフトウェア FB | **済。製品ではない。磨かない** |
| **S1** | compositor がソケットを持つ。step 1–2 がここに入る | **済（C クライアントまで）** |
| **S2** | 起動できるカーネルで `desktop.elf` を **Wayland クライアント**として exec | **やらない（今）**。`syscall.c` に名前があっても、from-source kernel を stub ISO に載せない。`-no-reboot` + 自前 kernel は QEMU が即死する。stub は daily `g1-desk` のまま |

PID1 は `init_tramp.elf`（ISO 上の名前は `init.elf`）。そのあと `exec_initrd("compositor.elf")`。日次 PID1 の `init.elf` とは別物。

---

## 日次デスク（別トラック。本デスクではない）

| もの | 状態 |
|------|------|
| Native kernel + POSIX + `g1-desk` | **済**。`bfree.iso` |
| Qt 机（EX / VW / TE + persist） | **済**。`QT_QPA_PLATFORM=bfree` が QEMU FB を **直接**塗る。Wayland ではない。GPU ではない |
| persist | `-drive file=persist.img,...` が要る。EX が `/persist/desk.txt` に `from-desk` |

これは step 4 まで残す。step 3 の実験は **stub ISO だけ**。

---

## GPU

**未着手。** 全部ソフトウェア FB dirty blit。QPA は OpenGL / Rhi off。DRM / EGL なし。step 3 が塗れて、step 4 のあと。

---

## Gate 1 / 製品 QML（別トラック。今はやらない）

日次 `desktop.elf` の上で `DesktopShell.qml` を boot ロードする道。

- boot は `skip DesktopShell.qml` が必須
- Gate 1（薄い QML IR Ready）は type-loader QThread が動かない（`libstdc++ threads=no`）
- `beginCreate` は `CR2=0xC` で死ぬ。やり直さない
- 本デスク step 4 は **Wayland クライアントとして** QML を載せる。bfree QPA の FB chrome に QML を足す話ではない

---

## やらないこと

- compositor `fork`(57)
- stub ISO に from-source `kernel.elf` を載せる / `make -C kernel`
- `BFREE_BOOT_GUI_FIRST=1` を日次カーネルに付ける
- `execve("desktop.elf")` を g1-desk でやる（bfree QPA が FB を奪う / 知らない名前は busybox）
- GTK
- 仮 Explorer を製品にする
- 日次 `bfree.iso` を stub で上書きする（step 4 まで）
- Qt hello で `bfree_guest_enter_preflighted_mmap_noreturn`（戻らないので親が blit できない）
- Qt hello で `bfree_guest_install_static_env`

---

## 手元コマンド

C デスクに戻す（緑の窓。53MB ELF は消さない）:

```
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
export PATH="$HOME/xshim:$HOME/x86_64-elf-toolchain/bin:$PATH"
BFREE_P8TEST_C=1 bash tools/build_compositor_stub_iso.sh
qemu-system-x86_64 -cdrom bfree-compositor-stub.iso -m 1024 -vga std -serial file:/tmp/bfree_comp_stub.log
```

日次机:

```
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
qemu-system-x86_64 -cdrom bfree.iso -m 1024 -vga std -serial file:/tmp/bfree_serial.log
```

stub は `-no-reboot` を付けない。
