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

## 現状スナップショット（M17 直後・本リポ BusyBox）

`guest/rootfs/bin/busybox` を objdump し、`syscall_dispatch.c` と突合した結果:

| 区分 | 本数 | 意味 |
|------|-----:|------|
| BusyBox が使う NR | **91** | 実バイナリ証拠 |
| 登録済み・薄くない | **67** | 第一地図で一応到達 |
| 登録済みだが **THIN** | **15** | 次に厚くする対象 |
| `ENOSYS` かつ非ゴール外 | **9** | 次に番号を足す対象 |
| `ENOSYS` かつ非ゴール | **0** | BusyBox 経路では非ゴール未使用 |
| musl-core `ENOSYS` | **0** | 番号は揃っている |
| musl-core THIN | **6** | 意味論が次の山 |

### BusyBox がまだ `ENOSYS` の NR（行動可能・優先 A）

| NR | 名前 | なぜ優先か |
|---:|------|-----------|
| 143 | `sched_getparam` | BusyBox 実参照 |
| 144 | `sched_setscheduler` | 同上 |
| 145 | `sched_getscheduler` | 同上 |
| 146 | `sched_get_priority_max` | 同上 |
| 147 | `sched_get_priority_min` | 同上 |
| 164 | `settimeofday` | 同上（`date -s` 系） |
| 204 | `sched_getaffinity` | 同上 |
| 227 | `clock_settime` | 同上 |
| 334 | `rseq` | 同上（新しいが BusyBox が参照） |

### BusyBox / musl が触る THIN（優先 B・番号はあるが薄い）

BusyBox 経路: `rt_sigaction`(13), `rt_sigprocmask`(14), `ioctl`(16), AF_UNIX 一式(41–54), `clone`(56), `futex`(202)

musl-core 追加: `rt_sigreturn`(15)

ここは「登録を増やす」より **意味論を BusyBox ash / musl スレッドが通るまで厚くする**。

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

## 次バッチの推奨順（M18 候補）

1. **A1:** BusyBox `ENOSYS` 9本（sched_* / settimeofday / clock_settime / rseq）
2. **B1:** `futex` WAIT 実ブロック + `clone` スレッド経路を musl 視点で厚くする
3. **B2:** `ioctl` termios / `rt_sig*` 配送を ash ジョブ制御が困らない水準へ
4. **C1:** ash_regress に `date`/`id`/`ln -s`/`readlink` を追加し、第二地図を自動回帰に載せる
5. **C2:** `gen_abi_second_map.py` を `phase3_guest_auto.sh` か専用ゲートから呼ぶ

INET・io_uring・namespaces は引き続き非ゴール。

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
