# ISO の材料

GitHub を clone した人向けです。`bfree.iso` は書きません。

## 1. hello ISO（表紙まで）

机ではありません。

```
cd bfree_x86_64
bash tools/make_hello_iso.sh
qemu-system-x86_64 -m 512 -cdrom bfree-hello.iso -vga std -serial file:/tmp/bfree_hello.log
```

成功: シリアルに `Jumping to userland`。画面は白い B-Free TRON のままです。

## 2. 机の ISO（サンプルデスクトップ）

いまの机は daily の `bfree.iso` です。同じ机を **別ファイル名** にします。kernel も desktop.elf も作り直しません。

手元に `bfree.iso` があるとき:

```
cd bfree_x86_64
bash tools/make_desk_iso.sh
qemu-system-x86_64 -cdrom bfree-desk.iso -m 1024 -vga std -serial file:/tmp/bfree_desk.log
```

成功: EX / TE の机が出る（今動いた画面と同じ）。

`bfree.iso` が無い clone でも、同じコマンドで机を作ります。材料は GitHub Release `desk-goldens-1` から取ります（`bfree.iso` は git に置きません。123MB で上限超え）。

## 3. 机でファイルを開く・保存する

同じ Qt 机の `/persist` です。stub の Explorer ではありません。

初回だけディスクを作る:

```
cd bfree_x86_64
bash tools/_f1_persist_img_scaffold.sh
```

机 + 保存ディスクで起動:

```
cd bfree_x86_64
qemu-system-x86_64 -cdrom bfree-desk.iso -m 1024 -vga std -serial file:/tmp/bfree_desk.log -drive file=persist.img,if=ide,index=0,media=disk,format=raw
```

机の上:

1. **EX** を押す → `/persist/desk.txt` を作る（中身 `from-desk`）
2. **VW** を押す → そのファイルを読む
3. QEMU を閉じて、同じコマンドでもう一度起動 → EX で `desk.txt` が残っている

シリアルの成功:

- `desk note created`
- `Explorer listing persist`
- `Viewer body=from-desk`

`bfree.iso` のまま起動しても同じです。`-drive` を付けないと、再起動で消えます。

## まだやらないこと

- `bfree.iso` を上書きする
- kernel を作り直して机に載せる
- stub の偽物 Explorer を机の代わりにする

本デスクの TODO（Native OS / Wayland / 本デスク / GPU は別）は `docs/HONDESK_TODOLIST.ja.md`。S0–S2 の記録は `docs/HONDESK_PHASES.ja.md`。
