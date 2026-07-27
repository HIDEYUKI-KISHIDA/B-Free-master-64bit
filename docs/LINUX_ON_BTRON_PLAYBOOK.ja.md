# Linux ソフトを BTRON 系（B-Free）で確実に動かす — ノウハウ本命

> 一言で言うと: **「syscall の穴埋め」ではなく「Linux プロセスとして生まれさせる」** が本命。  
> 実装トラック: 本文書 → `bfree_x86_64/kernel/freestanding/` の process entry / TLS / brk。

## 0. 「参考の本命」を平語で

あなたが探しているノウハウの正体は次のどれか。

| 聞き方 | 実体 | B-Free への当てはめ |
|--------|------|---------------------|
| Linux を Windows で動かす | **WSL1** | 「Linux アプリが Linux のまま syscall を吐き、カーネルが翻訳する」 |
| FreeBSD で Linux アプリ | **Linuxulator** | B-Free と同じクラス。**いちばん真似るべき本** |
| Wine | Windows→Linux | **逆**。今の目的には使わない |
| T-Kernel 2.0 | `tk_*` RTOS | **別 ABI**。Linux ソフトを動かす仕事ではない |

B-Free（BTRON 思想の 64bit OS）の上で **未改造の Linux 静的バイナリ** を動かす、というのが今のゴール。  
それは「BTRON アプリを Linux 化する」のではなく、**B-Free カーネルが Linux ABI ホストになる**こと（Linuxulator 型）。

```
[ BusyBox / musl 静的 ELF ]  --Linux syscall-->  [ B-Free カーネル ]
        ↑ 動かしたいソフト                          ↑ 今ここを厚くする
```

T-Kernel を動かす仕事（`tk2-*-virt`）とは地図を分ける。混ぜると優先度が壊れる。

---

## 1. Linuxulator / WSL1 から盗む「型」だけ

盗むのはソースではなく **チェックリスト**。

1. **Linux の生まれ方**を真似る  
   スタックに `argc / argv / envp / auxv` を載せ、`e_entry` へ飛ぶ  
   （C 呼び出し規約で `rdi=argc` は **Linux ではない**）
2. **TLS**（`FS` ベース）を本物にする — glibc/musl 静的の前提
3. **ヒープ**（`brk`/`mmap`）をユーザー VA に置く
4. **見える世界**を Linux に寄せる — `uname=Linux`、最小 `/proc`、`/dev`
5. **syscall 表**はそのあと — 落ちた NR だけ証拠で埋める

今の B-Free は 5 が進みすぎて、1–4 が薄い。だから ENOSYS を埋めても「確実に動く」に届かない。

---

## 2. 現状診断（なぜ BusyBox が「確実」ではないか）

実測（`guest/rootfs/bin/busybox`）:

- Entry `0x40fdb0` は glibc `_start`
- 最初に `pop %rsi`（**スタックから argc**）→ いまの trampoline（`rdi=argc`）は ABI 不一致
- `PT_TLS` あり → `arch_prctl(ARCH_SET_FS)` が MSR に書かないと死ぬ
- LOAD は ~1.2MiB → `execve` の 1KiB 読み制限だと子 exec 不可

| 層 | 今 | 確実動作に必要 |
|----|----|----------------|
| 起動 ABI | C 風 trampoline | Linux stack + auxv |
| TLS | 変数に保存のみ | `FS` MSR 書き込み |
| brk/mmap | カーネル heap ポインタ | identity map 上の user VA |
| execve | 小さいファイルのみ | マルチ MB ET_EXEC |
| uname | `"B-Free"` | `"Linux"`（アプリ判定用） |
| /proc | 無し | 最低 `/proc/self/exe` |
| syscall 表 | 広いが薄い | 落ちたものだけ厚く |

---

## 3. 実装順（これがノウハウの本体）

| 段 | 内容 | 完了条件 | 状態 |
|----|------|----------|------|
| **L0** | 本プレイブック固定 | 文書 + ゲート方針 | 本 PR |
| **L1** | Linux stack + auxv で `e_entry` へ | BusyBox `_start` が argc を読める | 済 |
| **L1b** | `uname` → `Linux`、`ARCH_SET_FS` → FS MSR | libc が自分を Linux と認識 / TLS 設定可 | 済 |
| **L2** | user VA の `brk`/`mmap` | `malloc` が生きる | 本 PR |
| **L3** | 大きい `execve` | ash が `/bin/busybox` を載せる | 次（blob 2MiB は L1 で一部） |
| **L4** | `/proc/self/exe`（+ maps） | BusyBox `CONFIG_BUSYBOX_EXEC_PATH` | その次 |
| **L5** | 証拠付きで stub を厚く | 第二地図 + guest 落ちログ | 継続 |

非ゴール（互換率の見せ金にしない）: io_uring、namespaces、フル INET、T-Kernel `tk_*` 混在。

---

## 4. 検証の仕方（「動いた気」を禁止）

- ホストで BusyBox を走らせる `phase3_guest_ash.sh` は **Linux ホスト上の互換**であり、B-Free 上の証明ではない
- QEMU guest で `ASH_*_GUEST_OK` が出ても、trampoline が BusyBox 内部を迂回しているなら証明が弱い
- L1 以降の正本ゲート: **Linux stack 経由で `_start` に入り**、既知マーカーを出す

ゲート: `test_p19_linux_process_abi`（stack ビルダ単体）+ QEMU ash（段階的に trampoline 撤去）

---

## 5. 参考リンク（読む順番）

1. FreeBSD Handbook — Linux Binary Compatibility（Linuxulator の型）
2. `linux(4)` — 何をカーネルが翻訳するか
3. System V ABI / Linux x86-64 — initial stack & auxv
4. 本 repo 第二地図 — 証拠で NR を決める（L5 用）
5. T-Kernel 2.0 仕様 — **TK トラック専用**。Linux-on-BTRON には使わない

---

## 6. まとめ

**探しているノウハウ = Linuxulator 型のプロセス誕生チェックリスト。**  
Wine でも T-Kernel 仕様でもない。  
次に書くコードは syscall 追加より、**stack/auxv → TLS → brk → execve → /proc** の順。
