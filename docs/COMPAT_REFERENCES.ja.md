# 互換率を上げるための参考地図（Wine / WSL / T-Kernel）

> 目的: 「聞いたことがあるソフト」と **このリポジトリで今やっている仕事** を取り違えない。  
> 正本の実装トラックは `docs/POSIX_FULL_COMPAT_ROADMAP.ja.md` と `docs/LINUX_ABI_SECOND_MAP.ja.md`。

## まず結論

| 聞き方 | 実体 | B-Free x86_64 での使い方 |
|--------|------|-------------------------|
| 「Linux を Windows で走らす」 | だいたい **WSL1**（Linux syscall → NT） | **方法論の参考**。地図の正本にはしない |
| 「Wine」 | Windows API → Linux/POSIX | **逆方向**。Linux guest 互換の正本ではない |
| 「T-Kernel 2.0 を動かす」 | `tk_*` の **別 ABI** | Linux BusyBox トラックとは **別マイルストーン** |
| 今の `bfree_x86_64` | Linux x86_64 syscall を自前カーネルが実装 | FreeBSD **Linuxulator** と同じクラス |

互換率を上げたい対象が **Linux 静的バイナリ（BusyBox/musl）** なら、参考の本命は WSL1 / Linuxulator / LTP / musl であり、Wine AppDB でも T-Kernel 仕様書でもない。

**平語の本命手順:** `docs/LINUX_ON_BTRON_PLAYBOOK.ja.md`  
（stack/auxv → TLS → brk → execve → /proc。ENOSYS 埋めはその後）

---

## 1. 似ているもの・似ていないもの

```
                    ゲストが発行する ABI
                 ┌──────────────┬──────────────┐
                 │ Linux syscall│ Windows API  │
    ┌────────────┼──────────────┼──────────────┤
    │ ホスト OS  │              │              │
    │ Windows NT │ WSL1         │ （ネイティブ）│
    │ Linux      │ （ネイティブ）│ Wine         │
    │ FreeBSD    │ Linuxulator  │              │
    │ B-Free     │ ★今ここ      │ （未）        │
    │ T-Kernel   │ （別物）      │ （別物）      │
    └────────────┴──────────────┴──────────────┘
```

### WSL1（いちばん近い「聞いたソフト」）

- Linux ユーザーランドが **そのまま** Linux syscall を発行
- カーネル側が NT のサービスへ **翻訳**
- 学べること: 「NR を埋める」より **意味論・パス・proc・シグナル** が落ちる
- 学べないこと: WSL の内部表をコピーしても B-Free のゲートにはならない

### FreeBSD Linuxulator（実装クラスとして本命）

- Linux ELF を brand して **別 syscall 表** に振る
- `/compat/linux` でパスを付け替え、Linux userland を載せる
- B-Free の `syscall_dispatch.c` + guest rootfs は同じ発想
- 参考: FreeBSD Handbook “Linux Binary Compatibility”、`linux(4)`

### QEMU linux-user

- ユーザー空間だけで syscall をホストへ翻訳
- 「どの NR が実アプリから来るか」の観測に使える
- 性能・正確な意味論の最終目標にはしない

### Wine（よく混同される）

- **Windows プログラムを Linux で**動かす（向きが逆）
- AppDB / syscall 地図は Windows 互換用
- セッション名「ワイン地図」は、実質 **Linux ABI 穴地図** の誤称だった

---

## 2. T-Kernel 2.0 との関係（別トラック）

T-Kernel 2.0 は μITRON 系の **自前 API（`tk_cre_tsk` 等）** を持つ RTOS 仕様。  
Linux の `read`/`clone`/`futex` とは番号も意味も違う。

| やりたいこと | 参考にするもの | この repo での位置 |
|--------------|----------------|-------------------|
| T-Kernel 自体を QEMU で動かす | 公式 T-Kernel 2.0 仕様、自前の `tk2-*-virt` / `tkernel2-*` ポート | **別リポジトリの仕事** |
| T-Kernel アプリ（`tk_*`）を動かす | T-Kernel/OS 仕様、既存 BSP | Linux ABI 表を埋めても進まない |
| BTRON 思想の OS の上で **Linux CLI** を動かす | WSL1 / Linuxulator / LTP / 第二地図 | **今の `bfree_x86_64` トラック** |
| T-Kernel 上に POSIX 層を載せる製品を真似る | 商用 eT-Kernel 等の POSIX 拡張の **発想だけ** | 実装は Linux ABI 側に寄せるのが現実的 |

つまり:

- **「T-Kernel 2.0 を動かす」互換率** → T-Kernel 仕様と自前 TK2 ポートを正本にする  
- **「BusyBox が Linux らしく動く」互換率** → 今の第二地図 + regress を正本にする  

両方やるなら **地図を二つに分ける**（混ぜると ENOSYS 埋めが止まらなくなる）。

関連（同一作者の TK2 系）:

- `HIDEYUKI-KISHIDA/tk2-rv64-virt` — T-Kernel 2.0 on QEMU virt (RISC-V)
- `HIDEYUKI-KISHIDA/tk2-LoongArch-la64-virt`
- `HIDEYUKI-KISHIDA/tkernel2-aarch64`

---

## 3. 互換率を上げるために本当に効く作業（ENOSYS 以外）

優先度は上から。

1. **実バイナリで踏ませる**  
   ash_regress / musl guest / LTP の最小プローブを増やす  
   （地図上の「登録済み」を「意味が足りる」に変える）

2. **既存 REG の意味論を厚くする**  
   guest で落ちた stub だけ直す（ioctl / poll / path / 時刻 / 権限）

3. **観測ツールを回し続ける**  
   `gen_abi_second_map.py`（BusyBox objdump × レジストリ）

4. **Linuxulator / WSL1 の「落ちやすい面」をチェックリスト化**  
   例: `/proc`・`/dev` の期待、`uname`、パス正規化、シグナル mask、ソケット domain  
   → ただし **証拠付き** でだけ実装

5. **INET / io_uring / namespaces**  
   非ゴールのまま。互換率の数字欲しさに触らない

6. **T-Kernel `tk_*` 実装**  
   Linux 互換率の数字には入らない。別 PR 系列にする

---

## 4. このリポジトリで次にやること（実行順）

| # | 内容 | 互換率への効き方 |
|---|------|------------------|
| A | `stat` applet + ash_regress（本バッチ） | BusyBox 実経路を固定 |
| B | guest で落ちた REG の厚み直し | 「動くアプリ数」が上がる |
| C | `/proc` `/sys` の最小スタブ（証拠が出たら） | musl/BusyBox が前提にするパス |
| D | TK2 トラックは別 repo / 別文書 | Linux 地図を汚染しない |

Wine AppDB を Linux 穴埋めの優先度表に使わない。  
T-Kernel 仕様書を Linux syscall の実装順に使わない。
