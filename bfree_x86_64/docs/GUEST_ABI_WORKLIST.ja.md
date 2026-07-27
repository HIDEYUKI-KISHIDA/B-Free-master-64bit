# ゲスト Linux ABI 作業リスト（9 + ~40–80）

更新: 2026-07-27  
対象: `kernel/sysmain/syscall.c` ほか process/vmm  
ゴール: Phase 5（NOFORK 解除）→ Phase 6（一般 musl 静的）へ進める実用 ABI  
非ゴール: Policy 94、未登録 syscall 全 352 本

検証ゲート（各バッチ後）:
```bash
bash tools/phase3_guest_auto.sh   # RESULT: ALL PASS 維持
# Phase5 以降は専用マーカーを追加
```

---

## トラック A — 明示 `return -38`（9 サイト）

| ID | 場所 | 内容 | 優先 | 作業 | 完了条件 |
|----|------|------|------|------|----------|
| A1 | Linux `case 57` | `fork` AS-copy | **P0** | ~~ENOSYS~~ **partial DONE**（eager AS-copy）。ゾンビ soft-reap 時に fork PT を解放し sticky EAGAIN を防止（2026-07-27） | `fork()` 成功＋親子別 AS；連続 fork+wait+fork で sticky EAGAIN なし |
| A2 | Linux `case 56` / `sys_linux_clone` | clone | **P0** | THREAD+VM → coop threads；VFORK → vfork；**それ以外 → AS-copy fork**（2026-07-27 **DONE**） | musl 静的が process-spawn `clone` で ENOSYS しない |
| A3 | Linux `case 247` | `waitid` = ENOSYS | **P0** | ~~wait4 相当を waitid ABI で実装~~ **DONE** | BusyBox/musl waitid 呼び出し OK |
| A4 | `sys_linux_execve` 非 fork 時 | fork 外 exec = -38 | **P0** | ~~単独 execve~~ **DONE**（in-place replace）+ 子は private AS 必須 | `BFREE_NOFORK_ALL=0` で外部 applet が Page Fault しない |
| A5 | `sys_mmap` fd 付き | ファイル mmap | **P1** | ~~ENOSYS~~ **partial DONE**：`/tmp` vfile を anon+memcpy；**R9 offset/pgoff 対応**（2026-07-27）。MAP_SHARED writeback は未 | open+mmap+read が `read()` と一致（offset 含む） |
| A6 | `sys_shm_open` | ENOSYS | **P2** | memfd へマップ、または最小 shm | 必要アプリが通るまで後回し可 |
| A7 | `sys_shm_unlink` | ENOSYS | **P2** | A6 とセット | 同上 |
| A8 | 旧 `sys_pipe` (B-Free 番号) | ホスト向け ENOSYS | **P3** | ゲスト Linux は `pipe2` 済み。触らない／削除候補 | ゲスト回帰に影響しない |
| A9 | unhandled default → -38 | 未登録番号すべて | **継続** | 下表 B の番号を 1 本ずつ dispatch に載せる | トレースで ENOSYS が減る |

**A の実質 P0 は A1–A4（プロセス／exec）。A5 がメモリ。A6–A8 は後回し可。**
A1/A2/A5 は 2026-07-27 スプリントで上記どおり更新。
---

## トラック B — 実用ギャップ（目標 40–80 本のうち、まず ~50）

「名前が無い／意味が浅い」を Phase 順に並べた。既に `case` があるものは **深化**、無いものは **新規**。

### B1 — Phase 5 ブロッカー（通常 CLI / NOFORK 解除）〜15 項目

| ID | syscall / 機能 | 状態 | 作業 |
|----|----------------|------|------|
| B1.1 | 複数子プロセス枠 | ~~1 子のみ~~ **ゾンビ最大 8** | zombie 表・複数 wait |
| B1.2 | `wait4`/`waitpid` 意味論 | 部分 | 複数ゾンビ掃引 |
| B1.3 | `waitid` | ~~ENOSYS~~ **DONE** | = A3 |
| B1.4 | `execve` 安定 | 部分 | = A4、プライベート AS 必須化 |
| B1.5 | `clone` 非 VFORK | ~~ENOSYS~~ **DONE**=A2 | = A2 |
| B1.6 | SIGCHLD 配送 | 弱い/無し | 子 exit で親に通知 |
| B1.7 | SIGPIPE / SIGINT | 弱い | パイプ切断・Ctrl+C |
| B1.8 | パイプ両端同時実行 | 協調/inproc | 複数 runnable または本物パイプ |
| B1.9 | BusyBox inproc pipe 撤去 | パッチ依存 | B1.1+B1.8 後 |
| B1.10 | bg-inline 撤去 | パッチ依存 | 同上 |
| B1.11 | `setpgid` / `getpgid` | 要確認・不足なら新規 | ジョブ制御下準備 |
| B1.12 | `setsid` / `getsid` / `getpgrp` | 要確認 | 同上 |
| B1.13 | `tcsetpgrp` / `tcgetpgrp` (ioctl) | 浅い | フォアグラウンド PG |
| B1.14 | `rt_sigaction` 配送 | 登録のみ寄り | 実際にハンドラ実行 |
| B1.15 | `BFREE_NOFORK_ALL=0` 回帰 | 失敗歴あり | **2026-07-18 再試験: kernel panic / 全滅 → NOFORK=1 にロールバック済。次は B1.6–B1.10** |

**完了条件:** `BFREE_NOFORK_ALL=0` で `phase3_guest_auto` 相当が ALL PASS。

### B2 — Phase 6 ブロッカー（一般 musl 静的）〜20 項目

| ID | syscall / 機能 | 状態 | 作業 |
|----|----------------|------|------|
| B2.1 | file-backed `mmap` | partial DONE | = A5 |
| B2.2 | `mremap` | 未登録多い | 実装 or 安全 ENOSYS→代替確認 |
| B2.3 | `pread64` / `pwrite64` | 要確認 | オフセット付き I/O |
| B2.4 | `readv` / 強化 `writev` | 部分 | iovec 完走 |
| B2.5 | `fsync` / `fdatasync` | 要確認 | no-op 可なら明示 |
| B2.6 | `flock` / fcntl ロック | 要確認 | 単一プロセスなら no-op |
| B2.7 | `clone` スレッドフラグ | ENOSYS | TLS + スケジューラ |
| B2.8 | futex 深化 | 部分 stub | WAIT 実待ち・WAKE |
| B2.9 | `set_robust_list` | get のみ？ | set 実装 |
| B2.10 | `exit_group` | exit と同居 | スレッド全終了 |
| B2.11 | `tgkill` / `rt_tgsigqueueinfo` | 要確認 | スレッドシグナル |
| B2.12 | `sigaltstack` | 要確認 | |
| B2.13 | `rt_sigreturn` | 要確認 | |
| B2.14 | `rt_sigtimedwait` / `rt_sigsuspend` | 要確認 | |
| B2.15 | `clock_nanosleep` | 番号注意 | Linux 230 と整合 |
| B2.16 | `clock_getres` | 要確認 | |
| B2.17 | `sysinfo` / `prlimit64` | 部分 | musl が期待する値 |
| B2.18 | `membarrier` / `rseq` | 未 | 空成功スタブで足りるか検証 |
| B2.19 | `getrandom` | あり | 品質確認のみ |
| B2.20 | 静的 hello/musl スモーク | 無し | `/tmp` に置いた静的 ELF 実行テスト |

**完了条件:** musl-gcc 静的 `hello` + 小規模 CLI がゲストで実行可。

### B3 — Qt / デスクトップ向け（Phase 6 後半〜）〜15 項目

| ID | syscall / 機能 | 優先 | 作業 |
|----|----------------|------|------|
| B3.1 | `epoll_*` 深化 | P1 | 既に case あり → 実イベント接続 |
| B3.2 | `eventfd` / `timerfd` | P1 | 既にある → Qt が使う意味論 |
| B3.3 | `memfd_create` | P1 | case 319 あり → 確認 |
| B3.4 | `poll`/`ppoll` | P1 | 深化 |
| B3.5 | `ioctl` FB/input | P1 | QPA 経路 |
| B3.6–B3.12 | socket 系 7 本 | P2 | socket/bind/connect/listen/accept/sendto/recvfrom（ネット無しなら後回し） |
| B3.13 | `getsockopt`/`setsockopt` | P2 | |
| B3.14 | `shutdown`/`socketpair` | P2 | socketpair はあり |
| B3.15 | 大スタック / TLS / 例外 | P1 | 既存 Qt デバッグ知見の定着 |

**完了条件:** `[desktop_qt] QML ready` が再現する、または明確な次のブロッカー 1 個に収束。

### B4 — バッファ（必要になったら）〜10–20

トレースで ENOSYS が出た番号だけ追加。例: `statfs` 深化、`mount`、`umount2`、`pivot_root`、`capset`、`userfaultfd` …  
**先にリスト固定しない。** A9 のログから足す。

---

## 推奨スプリント順（作業の掴み方）

```
Sprint 1 (1–2 週)  A4 exec安定 + B1.1–B1.4 複数子/wait
Sprint 2 (1–2 週)  B1.6–B1.10 シグナル＋パイプ → NOFORK=0 試験
Sprint 3 (2–4 週)  A1/A2 fork or スレッド clone + A3 waitid
Sprint 4 (2–4 週)  A5 mmap + B2 musl hello
Sprint 5+          B3 Qt
```

おおまか工数: **Phase5 到達 1–2 ヶ月、Phase6 musl さらに 1–2 ヶ月**（専任・連続作業想定）。Qt は別上乗せ。

---

## 進捗の数え方

| バケット | 初期 | 数え方 |
|----------|------|--------|
| A 明示 ENOSYS | 9 | サイト解消 or 「意図的残」と印 |
| B1 | 15 | チェックボックス |
| B2 | 20 | 同上 |
| B3 | 15 | 同上 |
| B4 | 可変 | トレース追加分 |
| **計画合計** | **約 9+50(+α) ≈ 60** | 80 上限の中核 |

---

## 関連ファイル

- `kernel/sysmain/syscall.c` — dispatch / ENOSYS
- `kernel/sysmain/process.c` — vfork スロット
- `kernel/sysmain/vmm.c` — AS / mmap
- `tools/phase3_guest_auto.sh` — 回帰
- `tools/build_guest_busybox.sh` — `BFREE_NOFORK_ALL`
- `docs/POSIX_FULL_COMPAT_ROADMAP.ja.md` — マイルストーン
- `docs/WINE_LINUX_ABI_MAP.md` — ホスト Wine strace → B-Free dispatch 差分地図（`tools/wine_linux_abi_map.sh`）
