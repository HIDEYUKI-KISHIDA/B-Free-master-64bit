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
| A5 | `sys_mmap` fd 付き | ファイル mmap | **P1** | ~~ENOSYS~~ **DONE（簡易）**：`/tmp` vfile を anon+memcpy；R9 offset；**MAP_SHARED は munmap/read 前 writeback**（2026-07-28）。真の CoW/ページ共有は未 | SHARED 書込が `read()` で見える；PRIVATE はコピー |
| A6 | `sys_shm_open` | vfile `shm/<name>` | **P2** | ~~ENOSYS~~ **DONE**（`bfree_guest_shm_open` → `/tmp` vfile + publish） | shm_open+mmap(+SHARED) が通る |
| A7 | `sys_shm_unlink` | vfile unlink | **P2** | ~~ENOSYS~~ **DONE**（`bfree_guest_shm_unlink`） | unlink 後 open が ENOENT |
| A8 | 旧 `sys_pipe` (B-Free 番号) | ホスト向け ENOSYS | **P3** | ゲスト Linux は `pipe2` 済み。触らない／削除候補 | ゲスト回帰に影響しない |
| A9 | unhandled default → -38 | 未登録番号すべて | **継続** | 下表 B の番号を 1 本ずつ dispatch に載せる | トレースで ENOSYS が減る |

**A の実質 P0 は A1–A4（プロセス／exec）。A5 がメモリ。A6–A8 は後回し可。**
A1/A2/A5–A7 は 2026-07-27〜28 スプリントで上記どおり更新。`memfd_create`（Linux 319）も vfile stub 配線済み（= B3.3）。
---

## トラック B — 実用ギャップ（目標 40–80 本のうち、まず ~50）

「名前が無い／意味が浅い」を Phase 順に並べた。既に `case` があるものは **深化**、無いものは **新規**。

### B1 — Phase 5 ブロッカー（通常 CLI / NOFORK 解除）〜15 項目

| ID | syscall / 機能 | 状態 | 作業 |
|----|----------------|------|------|
| B1.1 | 複数子プロセス枠 | ~~1 子のみ~~ **DONE**（ゾンビ最大 8） | zombie 表・複数 wait |
| B1.2 | `wait4`/`waitpid` 意味論 | **DONE**（wait 時 fork PT 解放を防御的に強化 2026-07-28） | 複数ゾンビ掃引 |
| B1.3 | `waitid` | ~~ENOSYS~~ **DONE** | = A3 |
| B1.4 | `execve` 安定 | **partial DONE** | = A4、プライベート AS 必須化 |
| B1.5 | `clone` 非 VFORK | ~~ENOSYS~~ **DONE**=A2 | = A2 |
| B1.6 | SIGCHLD 配送 | **DONE**（raise + CATCH 配送；p8/ash マーカー） | 子 exit で親に通知 |
| B1.7 | SIGPIPE / SIGINT | **DONE**（P8_SIGPIPE；コンソール VINTR→SIGINT） | パイプ切断・Ctrl+C |
| B1.8 | パイプ両端同時実行 | **DONE**（seq-fork AS-copy + coop；B2_3PIPE） | 複数 runnable または本物パイプ |
| B1.9 | BusyBox inproc pipe 撤去 | **DONE**（default KEEP_INPROC=0） | B1.1+B1.8 後 |
| B1.10 | bg-inline 撤去 | **DONE**（default off；FORK_BG→AS-copy） | 同上 |
| B1.11 | `setpgid` / `getpgid` | **DONE** | ジョブ制御下準備 |
| B1.12 | `setsid` / `getsid` / `getpgrp` | **DONE** | 同上 |
| B1.13 | `tcsetpgrp` / `tcgetpgrp` (ioctl) | **DONE**（P8_TTY） | フォアグラウンド PG |
| B1.14 | `rt_sigaction` 配送 | **DONE**（CATCH 実行；P8_SIGCHLD） | 実際にハンドラ実行 |
| B1.15 | `BFREE_NOFORK_ALL=0` 回帰 | **DONE**（2026-07-28 phase3 ALL PASS） | NOFORK=0 で ALL PASS |

**完了条件:** `BFREE_NOFORK_ALL=0` で `phase3_guest_auto` 相当が ALL PASS。

### B2 — Phase 6 ブロッカー（一般 musl 静的）〜20 項目

| ID | syscall / 機能 | 状態 | 作業 |
|----|----------------|------|------|
| B2.1 | file-backed `mmap` | **DONE（簡易）** | = A5 + SHARED |
| B2.2 | `mremap` | **DONE（簡易・非 MAYMOVE）** | in-place grow/shrink |
| B2.3 | `pread64` / `pwrite64` | **DONE** | P8_PREAD |
| B2.4 | `readv` / 強化 `writev` | **DONE（簡易）** | `sys_linux_readv` + 既存 `writev`；curated `fs_writev_readv` PASS |
| B2.5 | `fsync` / `fdatasync` | **DONE（no-op）** | stub あり・dispatch 欠落だった → case 74/75 配線；curated `fs_fsync` PASS |
| B2.5b | `umask` / path `truncate` | **DONE** | case 95 / 76；curated PASS。併せて vfile slots 16→64、`O_DIRECTORY=0200000` |
| B2.5c | `fchdir` / `faccessat` / `timerfd` / memfd·shm·socketpair | **DONE（簡易）** | 81/269/283–287；memfd vfile；`/dev/shm` マップ；双方向 socketpair |
| B2.5d | `prlimit64` / `msync` / `sync` / epoll packed | **DONE** | old rlimit 書き込み；26/162；legacy nr26→mmap 撤去；x86_64 packed epoll_event |
| B2.5e | `mincore` / AF_UNIX close リーク | **DONE** | case 27；close で unix slot+pipe reclaim。UNIX slots 16 |
| B2.5f | `prctl` name / eventfd CLOEXEC / getsockname UNIX / pipe2 flags | **DONE** | SET/GET_NAME；eventfd publish+CLOEXEC；AF_UNIX getsockname；O_NONBLOCK/O_CLOEXEC（誤 020000 修正） |
| B2.5g | getpeername UNIX / MSG_PEEK / O_EXCL / CLOEXEC / rusage·times | **DONE** | AF_UNIX getpeername；pipe MSG_PEEK；open O_EXCL；socket/epoll/timerfd CLOEXEC；getrusage/times/getpriority stub |
| B2.5h | inet write pipe / setgid / socketpair CLOEXEC / round-8 curated | **DONE** | inet `write`→pipe_magic；setgid；socketpair CLOEXEC；curated **215** |
| B2.5i | pure libc + fork/waitpid PF | **DONE** | curated **261**；`exit_from_fork` mode-2 wait+reap+parent PT+drop SIGCHLD；実 `proc_fork_wait` |
| B2.5j | curated round-9 pure libc | **DONE** | +41 math/wchar/stdio/string；fork 0x522ea0 fingerprint non-fatal；**302** |
| B2.5k | curated round-10 pure libc | **DONE** | +32 math/wchar/stdio/string/time/ctype；**334** |
| B2.5l | curated round-11 + exit_group busybox reload | **DONE** | +24；exit_group 親 AS 復帰＋`load_elf_image(busybox)`＋`exec_transfer_rip`；**358**；ポストスイート PF 解消 |
| B2.5m | curated round-12 pure libc | **DONE** | +24 math/wchar/stdio/string；**382** |
| B2.5n | curated round-13 pure libc | **DONE** | +24 mathf/wchar/stdio/string；**406** |
| B2.5o | curated round-14 bold pure libc | **DONE** | +71 mathf/complex/wchar/stdio/string/time/mkstemp；**477** |
| B2.5p | curated round-15 bold pure libc | **DONE** | +73 mathl/complex/locale/*rand48/wchar mbr*/inet/stdio；**550** |
| B2.5q | curated round-16 bold pure libc | **DONE** | +77 mathl/complexf/fpclassify/wchar/stdio/inet；**627** |
| B2.6 | `flock` / fcntl ロック | **DONE** | P8_FLOCK |
| B2.7 | `clone` スレッドフラグ | partial（coop THREAD） | TLS + スケジューラ |
| B2.8 | futex 深化 | 部分 stub | WAIT 実待ち・WAKE |
| B2.9 | `set_robust_list` | **DONE（no-op set）** | set 実装 |
| B2.10 | `exit_group` | **DONE** | exit と同居 |
| B2.11 | `tgkill` / `rt_tgsigqueueinfo` | 要確認 | スレッドシグナル |
| B2.12 | `sigaltstack` | **DONE** | |
| B2.13 | `rt_sigreturn` | **DONE** | |
| B2.14 | `rt_sigtimedwait` / `rt_sigsuspend` | **partial DONE**（`rt_sigsuspend`=130：soft-zombie+SIGCHLD+EINTR 2026-07-29） | timedwait 未 |
| B2.15 | `clock_nanosleep` | あり | Linux 230 と整合 |
| B2.16 | `clock_getres` | **DONE** | P8_MISC |
| B2.17 | `sysinfo` / `prlimit64` | 部分 | musl が期待する値 |
| B2.18 | `membarrier` / `rseq` | **DONE（空成功）** | |
| B2.19 | `getrandom` | **DONE** | 品質は簡易 |
| B2.20 | 静的 hello/musl スモーク | **追加**（`/musl_hello.elf`） | B2_MUSL_HELLO_OK |
| B2.21 | musl libc-test サブセット | **追加**（`libc_test_curated` **627**） | `LIBC_TEST_CURATED_RESULT` |
| B2.22 | `setitimer`/`getitimer`/`sched_getaffinity` | **DONE**（2026-07-29；ITIMER_REAL↔alarm；affinity=CPU0） | curated ENOSYS 0 |
| B2.23 | ash wait / waitpid heal | **DONE**（soft-zombie；1-reap；blocking `-1` yield；phase3 ALL PASS） | `cat FILE\|grep` 後期はハーネス回避・本修繕は次 |

**完了条件:** musl-gcc 静的 `hello` + 小規模 CLI がゲストで実行可。  
→ 2026-07-29: curated **627** + phase3 **ALL PASS** + desktop ENOSYS **0**（スタブ実体化スプリント）。Walls: thread / `cat FILE|grep` 後期 / TCP / cmdsubst PF.

### B3 — Qt / デスクトップ向け（Phase 6 後半〜）〜15 項目

| ID | syscall / 機能 | 優先 | 作業 |
|----|----------------|------|------|
| B3.1 | `epoll_*` 深化 | P1 | ~~部分~~ **DONE+**：pipe/eventfd/timerfd/sock ready を epoll_wait に接続（2026-07-28） |
| B3.2 | `eventfd` / `timerfd` | P1 | **DONE**（epoll 連携強化） |
| B3.3 | `memfd_create` | P1 | ~~確認~~ **DONE（簡易）**：`sys_linux_memfd_create` → vfile + ftruncate（A5 mmap 経路） |
| B3.4 | `poll`/`ppoll` | P1 | 既存あり・深化継続 |
| B3.5 | `ioctl` FB/input | P1 | QPA 経路（既存） |
| B3.6–B3.12 | socket 系 7 本 | P2 | socket/bind/connect/listen/accept/sendto/recvfrom（ネット無しなら後回し） |
| B3.13 | `getsockopt`/`setsockopt` | P2 | |
| B3.14 | `shutdown`/`socketpair` | P2 | socketpair はあり |
| B3.15 | 大スタック / TLS / 例外 | P1 | **定着**（256MiB mmap session stack；W3.5 まで GREEN） |

**完了条件:** `[desktop_qt] QML ready` が再現する、または明確な次のブロッカー 1 個に収束。  
→ **2026-07-28:** `QML ready` + W3.5 KEY PASS。次ブロッカー = `g_w3_sg_pixels=1` / DesktopShell 本読（早期 PF）。

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
