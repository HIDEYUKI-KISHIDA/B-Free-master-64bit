# ISO の材料（いま出せるもの）

GitHub を clone した人向けです。

## いま一本で作れるもの

**hello ISO**（カーネルの自己テスト + 埋め込み hello）。Qt の机ではありません。daily の `bfree.iso` でもありません。

```
cd bfree_x86_64
bash tools/make_hello_iso.sh
qemu-system-x86_64 -m 512 -cdrom bfree-hello.iso -display none -serial file:/tmp/bfree_hello.log
```

成功の目安（シリアル）: `STAGE1` または `user_hello.elf`。

このコマンドは **`bfree.iso` を書きません。**

## まだ一本で作れないもの

あなたの PC で動いている **Qt の机**（daily `bfree.iso`）です。

足りない材料:

- `desktop.elf`（本物の Qt ゲスト。10MB 未満は stub）
- daily 用 `kernel.elf.g1-desk`（ソースから組んだ kernel はまだ daily と同じではありません）
- `init.elf` / busybox / `iso_root` の机用 grub

これらは今 GitHub にありません。hello ISO が先、机の ISO は次です。

## やらないこと

- `bfree.iso` を上書きする
- `kernel.elf.g1-desk` を上書きする
- stub の偽物 Explorer を机の代わりにする
