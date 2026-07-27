# Linux ABI 第二地図（証拠ベース）

> 第一地図: `docs/LINUX_ABI_HOLES.ja.md`（NR表・Cat0–4・非ゴール宣言）  
> 第二地図: **実際のユーザーランドが叩く表面**で次に埋める順番を決める。  
> 参考切り分け: `docs/COMPAT_REFERENCES.ja.md`（WSL1 / Linuxulator / Wine / T-Kernel）  
> 生成補助: `bfree_x86_64/tools/gen_abi_second_map.py`

## なぜ第二地図が必要か

第一地図は「Linux x86_64 の穴一覧」としては有効だったが、次の混同が起きる:

1. **非ゴール**と **ただの long-tail** が同じ `ENOSYS` に見える
2. **登録済み**と **意味が足りる**が区別しにくい
3. Wine HQ の地図は Windows→POSIX 変換用で、このトラックの正本ではない

第二地図の正本は次の証拠源だけにする。

| 証拠源 | 役割 |
|--------|------|
| BusyBox 静的バイナリの `syscall` 即値（objdump） | 実 CLI が発行する NR |
| musl 静的 CRT / 共通 libc 表面（curated） | 一般静的 ELF が要る NR |
| `posix_regress/` / `ltp_regress/` / ash_regress | 既にゲート化した振る舞い |
| 第一地図の非ゴール宣言 | 埋めないもの |

```bash
cd bfree_x86_64
python3 tools/gen_abi_second_map.py
python3 tools/gen_abi_second_map.py --json /tmp/abi-second-map.json
```

## 現状スナップショット（M19 後）

| 区分 | 本数 | 意味 |
|------|-----:|------|
| BusyBox が使う NR | **97** | `date`/`id`/`ln`/`readlink` applet 追加後 |
| 登録済み・薄くない | **97** | 第二地図 actionable クリア（含 `faccessat2` 439） |
| 登録済みだが **THIN** | **0** | — |
| `ENOSYS` かつ非ゴール外（BusyBox） | **0** | — |
| musl-core `ENOSYS` / THIN | **0** / **0** | — |
| レジストリ `0..399` ENOSYS | **187** | long-tail + 非ゴール残 |

### M18 で埋めた ENOSYS 9本 + THIN 15本

（sched/clock/rseq、signals/`ioctl`/AF_UNIX/`clone`/`futex`）→ `test_p18_second_map_24`

### M19 で固定した ash 経路

| 追加 | ゲート |
|------|--------|
| BusyBox applet: `date`/`id`/`ln`/`readlink` | rootfs + initramfs seed |
| `ash_regress` 06–08 | host `phase3_guest_ash.sh` |
| QEMU trampoline マーカー | `ASH_DATE/ID/LN_READLINK_GUEST_OK` |

### M19 衛生バッチ（不完全ペア）

BusyBox 証拠ではないが、登録済みの片割れを埋めた薄い実装:

| NR | 名前 | 理由 |
|---:|------|------|
| 142 | `sched_setparam` | M18 sched の対 |
| 148 | `sched_rr_get_interval` | 同上 |
| 274 | `get_robust_list` | `set_robust_list` の対 |
| 286/287 | `timerfd_{settime,gettime}` | `timerfd_create` の対 |
| 439 | `faccessat2` | BusyBox 新 applet が発行（証拠） |

## 残 ENOSYS / long-tail の方針（埋めない・選ぶ）

**原則: 証拠が無い NR は埋めない。** 残 ~187 は「宿題リスト」ではなく、意図的な後回し＋非ゴール。

| 区分 | 目安 | 扱い |
|------|-----:|------|
| 未使用ギャップ 335–399 | ~65 | 触らない（実名 NR は 424+） |
| 非ゴール（aio/io_uring/ns/bpf/seccomp/ptrace…） | ~14+ | **永久 ENOSYS**（明示要求まで） |
| BusyBox 無関係 long-tail（modules/mq/keys/NUMA…） | ~40 | 証拠が来るまで放置 |
| musl 動的/TLS/タイマー拡張 | ~35 | 動的リンク guest を始めるとき再評価 |
| FS extras（xattr/splice/fanotify…） | ~30 | `tar`/`cp --xattrs` 等の証拠が出たら薄く |
| ネット **NR** | 0 | 足りないのは `AF_INET` **意味論**（非ゴール維持可） |

### 役に立つ次の作業（ENOSYS 埋め以外）

1. **regress 固定を増やす**（本 M19）— CLI が実 syscall 経路を踏む
2. **不完全ペア衛生**（本 M19 一部）— 片割れ ENOSYS を消す
3. **`AF_INET` 最小経路**（別マイルストーン）— NR 追加ではなく domain 実装
4. **既存 REG の意味を厚くする** — guest 負荷で落ちる stub を直す（地図より効く）
5. **第二地図を CI 相当で回し続ける** — `gen_abi_second_map.py`（phase3 済）

## 既存 regress

| スイート | 今あるもの |
|----------|------------|
| `ash_regress/` | pipe/subshell/cmdsubst/bg/external + **date/id/ln/readlink** |
| `posix_regress/` / `ltp_regress/` | 既存 raw/host ゲート |
| P17 / P18 | Cat0–4 + 第二地図バッチ |

## 次（M20 候補）

1. ~~`stat` applet + ash_regress~~ → M20 で実施
2. 新 BusyBox applet を足すときは必ず `gen_abi_second_map.py` で NR 差分を取る
3. INET・io_uring・namespaces は引き続き非ゴール
4. 参考の切り分け: `docs/COMPAT_REFERENCES.ja.md`（WSL1/Linuxulator vs Wine vs T-Kernel）

## 第一地図との関係

```
第一地図: 全 NR の穴・誤配線・Cat0–4・非ゴール宣言
    ↓ 完了後
第二地図: BusyBox/musl/regress 証拠で「次の1本」を決める
    ↓ actionable 0 の後
regress 固定 + 不完全ペア衛生 +（必要なら）domain 仕事
```

Wine（Windows 互換）の AppDB / syscall 地図は、このトラックの第二地図には使わない。

## 検証

```bash
cd bfree_x86_64
python3 tools/gen_abi_second_map.py
# 期待: BusyBox ENOSYS actionable = 0
./tools/phase3_guest_ash.sh
./build/host-tests/test_p17_abi_holes
./build/host-tests/test_p18_second_map_24
```
