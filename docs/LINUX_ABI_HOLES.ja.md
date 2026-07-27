# Linux ABI 穴インベントリ（0〜4）

> 基準ブランチ: `cursor/m16-ring3-fork-ash-695c`  
> 埋め込みマイルストーン: **M17 — ABI holes 0–4**  
> 方針: 未到達は `ENOSYS`。互換のための嘘の成功は禁止。誤配線は先に直す。

## 規模（M16 時点 → M17 埋め後）

| 指標 | M16 | M17（本変更後） |
|------|----:|----------------:|
| レジストリ実装マーク | 64 | **198** |
| `0..399` の `ENOSYS` | 336 | **202** |
| 誤 Linux NR 配線 | 3 | **0** |

検証: `test_p17_abi_holes` / `docs/M17_ABI_HOLES_0TO4.md`

## カテゴリ 0 — ABI 配線ミス（必須修正）

| Linux NR | 正しい名前 | M16 の誤り | 修正 |
|---:|---|---|---|
| 90 | `chmod` | `sys_capget`（常に 0） | 本物の `chmod` |
| 91 | `fchmod` | `sys_capset`（常に 0） | 本物の `fchmod` |
| 157 | `prctl` | `sys_prlimit64`（常に 0） | 本物の `prctl` |
| 125 | `capget` | 未登録 | 登録 + 実装 |
| 126 | `capset` | 未登録 | 登録 + 実装 |
| 302 | `prlimit64` | 未登録（157 に誤配線） | 登録 + 実装 |

## カテゴリ 1 — BusyBox / musl CLI 高優先 `ENOSYS`

`lstat`(6), `mprotect`(10), `munmap`(11), `pread64`(17), `pwrite64`(18), `readv`(19), `writev`(20), `access`(21), `select`(23), `madvise`(28), `uname`(63), `flock`(73), `fsync`(74), `fdatasync`(75), `truncate`(76), `ftruncate`(77), `fchdir`(81), `rename`(82), `mkdir`(83), `rmdir`(84), `creat`(85), `link`(86), `symlink`(88), `readlink`(89), `umask`(95), `gettimeofday`(96), `getrlimit`(97), `getrusage`(98), `sysinfo`(99), `getpgrp`(111), `getsid`(124), `gettid`(186), `tkill`(200), `time`(201), `set_tid_address`(218), `clock_gettime`(228), `clock_getres`(229), `clock_nanosleep`(230), `exit_group`(231), `tgkill`(234), `mknodat`(259), `fchownat`(260), `renameat`(264), `linkat`(265), `symlinkat`(266), `readlinkat`(267), `fchmodat`(268), `faccessat`(269), `pselect6`(270), `ppoll`(271), `set_robust_list`(273), `utimensat`(280), `epoll_create1`(291), `dup3`(292), `pipe2`(293), `prlimit64`(302), `renameat2`(316), `getrandom`(318), `statx`(332)

## カテゴリ 2 — 登録済みだが薄いスタブ

| NR | 名前 | 強化内容 |
|---:|---|---|
| 13–15 | `rt_sig*` | プロセス別アクション / マスク保存 |
| 16 | `ioctl` | `TCGETS`/`TCSETS` 等を許容 |
| 35 | `nanosleep` | timespec に応じて待機 / tick |
| 41–53 | UNIX socket | `listen`/`accept`/`shutdown` 追加 |
| 56 | `clone` | 非 THREAD → `fork`、`fn==NULL` 許容 |
| 202 | `futex` | WAIT で `*uaddr==val` 比較 |

## カテゴリ 3 — 中優先 POSIX / Linux

ネット完成（`accept`/`listen`/`getsockopt` 等）、SysV `sem*`/`msg*`、`epoll_*`、`chown` 系、`getgroups`/`setresuid` 系、`arch_prctl`(158)、`pause`/`alarm`/`sync`/`chroot`、`inotify_*`/`eventfd*`/`timerfd_*`（最小）、`memfd_create`/`execveat` 等。

## カテゴリ 4 — 非 syscall 構造穴（M16 Residual）

1. **プロセスごとの FD テーブル**（共有 VFS fd をやめる）
2. **プリエンプティブ・スケジューラ / timer tick**
3. **残 host LTP/POSIX probe の guest 移行**

## 非ゴール（埋めない）

- io_uring 全機能、名前空間完全互換、ホストとビット単位同一、INET フルスタック

## 検証

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh
./tools/test_p17_abi_holes   # または host-tests 経由
```
