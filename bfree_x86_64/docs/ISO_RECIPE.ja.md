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

`bfree.iso` が無い clone では、まだ机は作れません。必要な材料:

- `bfree.iso`（いちばん簡単）、または
- `kernel.elf.g1-desk` + 10MB 以上の `desktop.elf` + `init.elf`

## まだやらないこと

- `bfree.iso` を上書きする
- kernel を作り直して机に載せる
- stub の偽物 Explorer を机の代わりにする
