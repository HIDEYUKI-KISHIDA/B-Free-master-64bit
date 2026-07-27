# Linux ABI 第二地図（証拠ベース）

> 第一地図: `docs/LINUX_ABI_HOLES.ja.md`（NR表・Cat0–4・非ゴール宣言）  
> 第二地図: **実際のユーザーランドが叩く表面**で次に埋める順番を決める。  
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

## 現状スナップショット（M18 第二地図バッチ後）

M18 で第二地図の「次の24」を埋めた後:

| 区分 | 本数 | 意味 |
|------|-----:|------|
| BusyBox が使う NR | **91** | 実バイナリ証拠 |
| 登録済み・薄くない | **91** | A+B 完了（THIN クリア） |
| 登録済みだが **THIN** | **0** | M18 で厚くした |
| `ENOSYS` かつ非ゴール外 | **0** | BusyBox 経路の穴なし |
| musl-core `ENOSYS` | **0** | 番号は揃っている |
| musl-core THIN | **0** | M18 でクリア |

### M18 で埋めた ENOSYS 9本

| NR | 名前 |
|---:|------|
| 143 | `sched_getparam` |
| 144 | `sched_setscheduler` |
| 145 | `sched_getscheduler` |
| 146 | `sched_get_priority_max` |
| 147 | `sched_get_priority_min` |
| 164 | `settimeofday` |
| 204 | `sched_getaffinity`（+203 `sched_setaffinity`） |
| 227 | `clock_settime` |
| 334 | `rseq` |

### M18 で厚くした旧 THIN 15本

`rt_sigaction`/`rt_sigprocmask`/`ioctl`(termios+winsize)/AF_UNIX accept·recv 再試行/`clone`(FD継承·tid)/`futex`(協調 wait+tick)

検証: `test_p18_second_map_24`

## 非ゴールの切り分け（第二地図の外）

第一地図の非ゴールを、残 `ENOSYS` 202 本から分離する。

| 非ゴール | 第二地図での扱い | 目安本数（0..399） |
|----------|------------------|-------------------:|
| io_uring 全機能 | 埋めない。バッチ対象外 | ~9 |
| 名前空間完全互換 | 埋めない（`setns`/landlock/新 mount API） | ~12 |
| INET フルスタック | NR追加ではなく `AF_INET` 意味論。今は拒否のまま | （domain） |
| ホストとビット単位同一 | 性能/FSレイアウト。syscall表の外 | — |
| 周辺（bpf/seccomp/ptrace…） | 原則外。明示要求が来るまで触らない | ~6 |

**重要:** 残202の約87%は非ゴールではなく long-tail。ただし第二地図では **BusyBox/musl 証拠がない限り後回し**。

## 既存 regress とのギャップ（優先 C）

今のゲートは「デモが通る」止まりで、第二地図の穴を直接は測っていない。

| スイート | 今あるもの | 第二地図が欲しい次 |
|----------|------------|-------------------|
| `ash_regress/` | pipe/subshell/cmdsubst/bg/external | `date`/`id`/`ln`/`readlink`/`stat` を guest で |
| `posix_regress/` | host の P4/P5 ラッパ | BusyBox 優先 A の専用マーカー |
| `ltp_regress/` | open/trap/paging/user_boot | sched/clock/rseq の最小 probe |
| P17 | Cat0–4 の穴埋め完了確認 | `gen_abi_second_map.py` を CI 相当で定期実行 |

## 次バッチの推奨順（M19 候補）

1. ash_regress に `date`/`id`/`ln -s`/`readlink` を追加し第二地図を回帰固定
2. 残 long-tail のうち BusyBox 外・musl 動的/ネット拡張が要るものだけを新証拠で拾う
3. INET・io_uring・namespaces は引き続き非ゴール

## 第一地図との関係

```
第一地図: 全 NR の穴・誤配線・Cat0–4・非ゴール宣言
    ↓ 完了後
第二地図: BusyBox/musl/regress 証拠で「次の1本」を決める
```

Wine（Windows 互換）の AppDB / syscall 地図は、このトラックの第二地図には使わない。

## 検証

```bash
cd bfree_x86_64
python3 tools/gen_abi_second_map.py
# 期待: BusyBox ENOSYS actionable が「次バッチ」一覧と一致すること
./build/host-tests/test_p17_abi_holes
```
