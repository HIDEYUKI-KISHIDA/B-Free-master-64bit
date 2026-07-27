# B-Free x86_64 — Linux/POSIX 完全互換トラッキング

> **目的:** BusyBox / musl ゲストが「通常の Linux CLI」として動く状態を経て、最終的に Linux ABI / POSIX ユーザーランド互換を目指す。  
> **方針:** 一発実装ではなく、検証可能な小さな PR を積み上げる。未到達は `ENOSYS` で明示する。  
> **参考切り分け:** Wine / WSL1 / Linuxulator / T-Kernel → `docs/COMPAT_REFERENCES.ja.md`  
> **Linux-on-BTRON 本命:** `docs/LINUX_ON_BTRON_PLAYBOOK.ja.md`（プロセス誕生 L1→L5）

## 現状（2026-07）

ゲスト（`bfree_x86_64`）は協調的な単一タスク + 合成 FS 上で BusyBox ash の回帰（Phase 3 / Phase 4 基盤）を通している。

既に入っているもの（リポジトリ `bfree_x86_64/kernel/sysmain/`）:

- `/tmp` 階層 vnode、独立 open-file description、永続ブロック FS
- 協調的 `vfork` → 子 AS への `execve` → `exit` → `wait4` / `waitid`（複数 zombie）
- eager-copy `fork`、パイプ両端、SIGCHLD / SIGINT / SIGPIPE
- M3 shell CLI ハーネス + upstream BusyBox guest rootfs（`shell_cli.c`、`build_guest_busybox.sh`、`ash_regress/`）
- M4 syscall 層（`guest_io.c`、`elf_load.c`、`thread.c`、`tty.c` — read/write/dup2/brk/mmap/clone/futex/job control）
- 非 VFORK `clone` / スレッドは当面 `ENOSYS`（嘘の成功を返さない）

明示 `-ENOSYS` は個別に数本 + **未登録 Linux syscall の default**。Linux x86_64 の syscall は数百本あり、dispatch に載っているのはおおよそ 100 本弱で、その多くも意味論が狭いスタブである。

## マイルストーン（PR 単位の積み上げ）

### M0 — トラッキング（本 PR）

- ゴールと非ゴールの固定
- 以降の PR はこの文書のチェックリストを更新する

### M1 — ファイルシステム（Phase 4A–D） ✅ 完了

- [x] 独立 OFD（別 `open` = 別オフセット、`dup` = 共有）
- [x] `/tmp` 実ディレクトリ（mkdir/rmdir/ネスト/chdir）
- [x] dirent カーソルをすべて OFD へ（`kernel/sysmain/fs_ofd.c`、検証 `P4_DIRENT_OFD`）
- [x] unlink-while-open / vnode 寿命（`nref` + `unlinked`、検証 `P4_UNLINK_OPEN`）
- [x] `openat`/`*at` の dirfd 相対解決（`openat`/`mkdirat`/`unlinkat`、検証 `P4_OPENAT`）
- [x] 永続ブロック FS（`blk_vol.c`/`blk_persist.c`、検証 `P4_BLOCK_FS`）

### M2 — プロセス（最小の正直なモデル） ✅ 完了

- [x] 1 子・child-first `vfork`（`bfree_vfork`/`bfree_spawn_vfork_child`、検証 `P4_VFORK_EXEC`）
- [x] 子専用ページテーブルへの `execve`（登録済み applet 再実行、検証 `P4_VFORK_EXEC`）
- [x] `fork` = eager-copy AS（`bfree_as_fork_copy`、検証 `P4_FORK`）
- [x] applet reexec 基盤（`bfree_proc_register` — BusyBox NOFORK-all 緩和の前提、実パッチ撤去は M3）
- [x] 複数 zombie / 正しい `waitid`（検証 `P4_WAITID`）
- [x] プリエンプティブな複数 runnable + パイプ両端同時実行（`bfree_switch_proc` + pipe、検証 `P4_PIPE_SIGNAL`）
- [x] シグナル配送（SIGCHLD / SIGINT / SIGPIPE、検証 `P4_PIPE_SIGNAL`）

### M3 — 通常 CLI（BusyBox パッチ巻き戻し） ✅ 完了

- [x] inproc pipe / bg-inline の撤去（`bfree_shell_pipeline` / `bfree_shell_bg`、検証 `P4_PIPE_FORK` / `P4_BG_JOB`）
- [x] NOFORK-all 撤去方針・ビルド検査（`docs/M3_BUSYBOX_PATCH_ROLLBACK.md`、`build_guest_busybox.sh`、検証 `P4_EXTERNAL`）
- [x] 外部コマンド・コマンド置換・サブシェル（`shell_cli.c`、検証 `P4_CMDSUBST` / `P4_SUBSHELL` / `P4_SHELL_CLI`）
- [x] ゲスト ash 回帰（`phase3_guest_ash.sh` + `tools/ash_regress/`、upstream BusyBox rootfs、検証 `ASH_*`）

### M4 — musl / 一般静的バイナリ ✅ 完了（ホスト回帰）

- [x] 主要 syscall（`read`/`write`/`lseek`/`dup2`/`fcntl`/`chdir`/`getcwd`/`brk`/`mmap`、検証 `P4_RW_DUP2` / `P4_BRK_MMAP`）
- [x] ELF64 `execve` ローダ（`elf_load.c`、検証 `P4_ELF_LOAD`）
- [x] スレッド（`clone` CLONE_VM|CLONE_THREAD、`futex` wake、検証 `P4_CLONE_FUTEX`）
- [x] tty ジョブ制御基盤（`setsid`/`setpgid`/`tcsetpgrp`、検証 `P4_TTY_JOB`）
- [x] musl 静的バイナリ実機ゲスト実行（ホスト `elf_host_run` シム + 検証 `P4_MUSL_LOAD` / `P4_MUSL_EXEC`；ブート統合後は syscall trap へ移行）

### M5 — POSIX / Linux 完全互換（最終 PR 群）

- [x] ネット・IPC・権限・マウント・デバイスノード（`net_unix.c` / `ipc_shm.c` / `cred.c` / `mount.c` / `devnode.c`、検証 `P5_*`）
- [x] 未実装 syscall の `ENOSYS` レジストリ（`syscall_dispatch.c`、検証 `P5_SYSCALL_GATE`）
- [x] POSIX テストスイート / LTP のゲート（ホスト `posix_regress/` + `phase3_guest_posix_regress.sh`；フル LTP はブート後）

### M6 — ブート / syscall trap（ホスト回帰）

- [x] ランタイム syscall dispatch（`bfree_invoke_syscall`、検証 `P6_SYSCALL_INVOKE`）
- [x] x86_64 trap エントリスタブ（`syscall_entry.S`、検証 `P6_TRAP_ENTRY`）
- [x] QEMU ブートスモーク（`boot_smoke.S`、検証 `P6_BOOT_SMOKE` — QEMU 無しは SKIP）

### M7 — カーネルブート / in-kernel trap（ホスト回帰）

- [x] IDT / syscall MSR 状態初期化（`trap_setup.c`、検証 `P7_TRAP_SETUP`）
- [x] カーネル main（`kernel_main.c`、検証 `P7_KERNEL_MAIN`）
- [x] trap 経由静的ペイロード（`trap_payload.c`、検証 `P7_TRAP_PAYLOAD`）
- [x] musl static trap-init exec（`elf_trap_exec.c`、検証 `P7_MUSL_TRAP`）
- [x] QEMU `kernel.elf` ブート（`boot_kernel.S`、検証 `P7_KERNEL_BOOT` — QEMU 無しは SKIP）
- [x] LTP スタイルゲート（`ltp_regress/` + `phase3_guest_ltp.sh`）

### M8 — 実ブート経路（ホスト + QEMU）

- [x] 恒等ページング（`paging.c`、検証 `P8_PAGING`）
- [x] newc cpio initramfs パーサ（`initramfs.c`、検証 `P8_INITRAMFS`）
- [x] ハードウェア trap インストール（`trap_hw.c` lidt/wrmsr、検証 `P8_TRAP_HW`）
- [x] フリースタンディング C カーネル + 埋め込み initramfs（`kernel_boot.c`、`kernel.elf`、検証 `P8_KERNEL_BOOT`）

### M9 — ring-3 ユーザーブート（ホスト + QEMU） ✅ 完了

- [x] GDT 構築（`gdt.c`、検証 `P9_GDT`）
- [x] `syscall` インストラクションエントリ（`syscall_insn_entry.S`）
- [x] initramfs からユーザペイロード起動（`user_boot.c` / `user_boot_ring3.S`、検証 `P9_USER_BOOT`）
- [x] QEMU `USER_BOOT_OK`（`user_payload.S`、検証 `P9_USER_BOOT_QEMU`）

ホーム PC 手順: `docs/HOME_PC_SETUP.md`

### M10 — sysmain in kernel.elf ✅ 完了

- [x] `syscall_min.c` を `KERNEL_SYSMAIN_OBJS` に置換
- [x] dispatch ギャップ修正（`openat`, `dup`, `getdents64`, …）
- [x] `/dev/console` → debugcon（QEMU `USER_BOOT_OK`）
- [x] 検証 `P10_DISPATCH_WIRING`, `P10_KERNEL_GUEST`

手順: `docs/M10_SYSCALL_KERNEL.md`

### M11 — musl on booted guest ✅ 完了

- [x] musl `ET_EXEC` in initramfs (`musl_static.elf`)
- [x] Freestanding ELF user loader (`elf_user_load.c`)
- [x] `BFREE_PREFER_MUSL_BOOT` kernel boot path
- [x] 検証 `P11_MUSL_GUEST`, `P11_MUSL_GUEST_QEMU`

手順: `docs/M11_MUSL_GUEST.md`

### M12 — BusyBox ash on booted guest ✅ 完了

- [x] BusyBox static `ET_EXEC` in initramfs (`bin/busybox`)
- [x] Ring-3 argv trampoline (`ash_guest_tramp.S`, `ash_guest_boot.c`)
- [x] `BFREE_PREFER_BUSYBOX_BOOT` kernel boot path
- [x] 検証 `P12_BUSYBOX_GUEST`, `P12_BUSYBOX_GUEST_QEMU`

手順: `docs/M12_BUSYBOX_GUEST.md`

### M13 — guest LTP/POSIX on booted QEMU ✅ 完了

- [x] Guest probe ELFs (`ltp_open_guest.elf`, `posix_io_guest.elf`) in initramfs
- [x] `BFREE_PREFER_LTP_OPEN_BOOT` / `BFREE_PREFER_POSIX_IO_BOOT` boot paths
- [x] 検証 `P13_GUEST_REGRESS`, `P13_LTP_OPEN_GUEST_QEMU`, `P13_POSIX_IO_GUEST_QEMU`

手順: `docs/M13_GUEST_LTP_POSIX.md`

### M14 — stat / poll / fstatat ✅ 完了

- [x] `stat` (4), `fstat` (5), `poll` (7), `newfstatat` (262) in syscall path
- [x] Guest probes `stat_guest.elf`, `poll_guest.elf` in initramfs
- [x] 検証 `P14_STAT_POLL`, `P14_GUEST_STAT_POLL`, QEMU smokes

手順: `docs/M14_STAT_POLL.md`

### M15 — guest ash regress + ring-3 execve ✅ 完了

- [x] Initramfs → VFS seed (`guest_initramfs_seed.c`)
- [x] Ring-3 `execve` (`elf_user_exec.c`)
- [x] Blocking `poll` (busy-wait timeout)
- [x] Ash subshell/cmdsubst on QEMU (`ASH_*_GUEST_OK`)
- [x] 検証 `P15_POLL_BLOCKING`, `P15_ASH_REGRESS_STAGED`, `P15_ASH_REGRESS_GUEST_QEMU`

手順: `docs/M15_GUEST_ASH_EXECVE.md`

### M16 — ring-3 fork / full ash QEMU ✅ 完了

- [x] Ring-3 `fork`/`vfork` + pipe `dup2` + blocking wait
- [x] Full ash regress on QEMU (`ASH_*_GUEST_OK`)

手順: `docs/M16_RING3_FORK_ASH.md`

### M17 — Linux ABI holes 0–4 ✅ 完了

- [x] Cat0: 誤 NR 配線修正（`chmod`/`fchmod`/`prctl`/`cap*`/`prlimit64`）
- [x] Cat1: BusyBox/musl 高優先 `ENOSYS` 埋め（登録 64→198、`0..399` ENOSYS 336→202）
- [x] Cat2: 薄いスタブ強化（signals/`ioctl`/`nanosleep`/listen·accept/`clone`/`futex`）
- [x] Cat3: 中優先 POSIX（SysV sem/msg、epoll、cred groups、…）
- [x] Cat4: プロセス別 FD、`bfree_sched_tick`、P17 ゲート

手順: `docs/M17_ABI_HOLES_0TO4.md` / `docs/LINUX_ABI_HOLES.ja.md`

### M18 — 第二地図バッチ（BusyBox 9 ENOSYS + 15 THIN）

- [x] 証拠ベース優先: `docs/LINUX_ABI_SECOND_MAP.ja.md`
- [x] 生成: `bfree_x86_64/tools/gen_abi_second_map.py`
- [x] BusyBox 残 `ENOSYS` 9本（sched_*/settimeofday/clock_settime/rseq + setaffinity）
- [x] 旧 THIN 15本を厚く（signals/`ioctl` termios/AF_UNIX retry/`clone` FD·tid/`futex`）
- [x] ゲート: `test_p18_second_map_24`（BusyBox 当時 91/91、actionable ENOSYS 0）

### M19 — ash_regress 固定 + long-tail 方針 + 不完全ペア衛生

- [x] BusyBox applet: `date`/`id`/`ln`/`readlink`（`configs/busybox_m3.config`）
- [x] `ash_regress` 06–08 + QEMU trampoline マーカー + initramfs seed
- [x] 証拠で出た `faccessat2`(439) を登録
- [x] 不完全ペア薄実装: `sched_setparam`/`sched_rr_get_interval`/`get_robust_list`/`timerfd_*`
- [x] 残 ENOSYS〜187 は **証拠無しでは埋めない**（第二地図に方針記載）
- 次（M20）: `stat` applet 任意、`AF_INET` は別トラック、long-tail 一括埋めはしない

### M20 — 参考地図の固定 + `stat` applet

- [x] `docs/COMPAT_REFERENCES.ja.md`（WSL1 / Linuxulator / Wine / T-Kernel の切り分け）
- [x] BusyBox `stat` + ash_regress 09 + QEMU マーカー
- 方針: T-Kernel `tk_*` は別トラック。Linux 互換率は Linuxulator 級の証拠駆動で上げる

### M21 — Linux-on-BTRON プロセス ABI（プレイブック L1/L1b）

- [x] `docs/LINUX_ON_BTRON_PLAYBOOK.ja.md`（本命ノウハウの固定）
- [x] Linux stack + auxv ビルダ（`linux_user_stack.c`）
- [x] ash / ash_regress / execve を **e_entry + Linux stack** へ（C 風 trampoline 撤去）
- [x] `uname` → `Linux`、`ARCH_SET_FS` → FS MSR（guest）
- [x] ゲート: `test_p19_linux_process_abi`

### M22 — Linux-on-BTRON L2（user VA brk/mmap）

- [x] `bfree_as_init_user_va` — identity map 上の絶対 VA heap
- [x] guest pid1 を user VA モードへ（`guest_kernel.c`）
- [x] stack top `0x2000000`、heap `0x540000`、mmap は下向き
- [x] ゲート: `test_p20_user_va_brk_mmap`

### M23 — Linux-on-BTRON L3/L4（multi-MB execve + `/proc/self/exe`）

- [x] exec blob 上限 8MiB + symlink follow（`elf_user_exec.c`）
- [x] 最小 procfs: `/proc/self/exe` + `/proc/self/maps`（`procfs.c`）
- [x] exec / ash boot で procfs 更新
- [x] ゲート: `test_p21_multi_mb_execve` / `test_p22_proc_self_exe`

### M24 — Linux-on-BTRON L5/L6（procfs 実用層）

- [x] `/proc/self/status`・`/proc/meminfo`・`/proc/cpuinfo`
- [x] `/proc/uptime`・`/proc/self/stat`・`/proc/self/auxv`
- [x] `exec` 後に `status/stat` の Name 更新
- [x] ゲート: `test_p23_procfs_basic` / `test_p24_procfs_runtime`
- 次（L7）: 証拠落ち syscall / signal frame を優先して厚くする

### M25 — Linux-on-BTRON L7（signal ABI の正直化）

- [x] `rt_sigaction` / `rt_sigprocmask` で `sigsetsize==8` を検証（不正は `-EINVAL`）
- [x] `rt_sigreturn` はフレーム未実装のため `-ENOSYS` を返す（偽成功を撤去）
- [x] レジストリを実態へ整合（NR 15 は implemented 扱いから外す）
- [x] ゲート: `test_p25_signal_abi_honesty`

### M26 — Linux-on-BTRON L8（rt_sigreturn 復元）

- [x] 最小 Linux x86_64 `rt_sigframe` / `ucontext` / `sigcontext`
- [x] `bfree_rt_sigframe_setup` でフレームをプロセスへ装着
- [x] `rt_sigreturn` が `uc_sigmask` と RIP/RSP/RAX を復元
- [x] フレーム未装着時は `-EFAULT`（偽成功禁止）
- [x] ゲート: `test_p26_rt_sigreturn`

### M27 — Linux-on-BTRON L9（ring-3 signal delivery）

- [x] `bfree_rt_signal_poll_deliver` — pending → handler frame on user stack
- [x] `kill` / ring3 post-syscall から配送
- [x] default restorer stub（`bfree_signal_restorer` → NR15）
- [x] sa_mask + 自シグナルを handler 中ブロック、sigreturn で復元
- [x] ゲート: `test_p27_signal_deliver`
- 次（L10）: entry trampoline で rdi/rsi/rdx を正式にセット（SA_SIGINFO）

手順: `docs/LINUX_ON_BTRON_PLAYBOOK.ja.md`

## 非ゴール（このトラックでは約束しない）

- 一 PR での「POSIX 全部」
- ホスト Linux とビット単位で同じ性能・同じ FS レイアウト
- すべての Linux 拡張（io_uring 全機能、名前空間完全互換など）を初期から揃えること

## ENOSYS 方針

| 種別 | 扱い |
|------|------|
| 未実装 syscall | `ENOSYS`（default） |
| 部分実装（例: VFORK なし clone） | `ENOSYS` または `EINVAL` |
| 互換のため嘘の成功 | **禁止**（過去の罠） |

`fork` を協調 `vfork` に戻すのは「互換の見た目」であり POSIX fork ではない。M2 以降は **eager-copy fork**（`bfree_as_fork_copy`）を提供する。プリエンプティブなスケジューラは M4 以降。

## 検証ゲート

各マイルストーン PR は少なくとも次を通す:

1. `tools/phase3_guest_auto.sh` → `RESULT: ALL PASS`
2. その PR が追加した専用マーカー（例: `P4_*`）
3. カーネル panic / 連続 GP 無し

## 関連ローカルパス

実装の主戦場:

- `bfree_x86_64/kernel/sysmain/fs_ofd.c`
- `bfree_x86_64/kernel/sysmain/blk_vol.c`
- `bfree_x86_64/kernel/sysmain/blk_persist.c`
- `bfree_x86_64/kernel/sysmain/syscall.c`
- `bfree_x86_64/kernel/sysmain/process.c`
- `bfree_x86_64/kernel/sysmain/vmm.c`
- `bfree_x86_64/kernel/sysmain/shell_cli.c`
- `bfree_x86_64/kernel/sysmain/guest_io.c`
- `bfree_x86_64/kernel/sysmain/elf_load.c`
- `bfree_x86_64/kernel/sysmain/thread.c`
- `bfree_x86_64/kernel/sysmain/tty.c`
- `bfree_x86_64/kernel/sysmain/elf_host_run.c`
- `bfree_x86_64/tools/phase3_guest_auto.sh`
- `bfree_x86_64/tools/phase3_guest_ash.sh`
- `bfree_x86_64/tools/phase3_guest_posix_regress.sh`
- `bfree_x86_64/tools/posix_regress/`
- `bfree_x86_64/tools/build_musl_static.sh`
- `bfree_x86_64/tools/build_guest_busybox.sh`
- `docs/M3_BUSYBOX_PATCH_ROLLBACK.md`
