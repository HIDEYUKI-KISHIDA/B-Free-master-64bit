# 次の指示（スマホ編集用）

このファイルは **GitHub モバイルから編集して指示を残す**ためのものです。
Cursor エージェントはセッション開始時にここを読みます。

## スマホでの出し方

1. GitHub アプリでリポジトリ `B-Free-master-64bit` → ブランチ `work/posix-holes-redo` を開く
2. ファイル: `bfree_x86_64/docs/NEXT_INSTRUCTIONS.md`
3. ✏️ → **下の「スマホ指示」欄だけ**を書き換える → Commit
4. PC で Cursor を開く（「NEXT を見て」でも可。ルールで自動参照）

直リンク（アプリで開く）:
https://github.com/HIDEYUKI-KISHIDA/B-Free-master-64bit/blob/work/posix-holes-redo/bfree_x86_64/docs/NEXT_INSTRUCTIONS.md

---

## スマホ指示（ここだけ編集）

<!-- PHONE_START: この行と PHONE_END の間に書いて Commit -->

次へ

<!-- PHONE_END -->

書き方の例:
- `次へ`
- `W3.3 で sustained SG を試せ`
- `止まって。スモークだけ回せ`
- `ISO を作り直して`

---

## 常設方針（エージェント用・スマホでは触らなくてよい）

**正本:** `C:\Users\h_kis\Desktop\B-Free-master`（詳細: `docs/CANONICAL.md`）。`J:\B-Free-master` は古いコピー・使わない。
本線: `work/posix-holes-redo`。**実用 Desktop = FB 画素権威**（W3.5 hybrid）。SG／DesktopShell 本読は別トラック。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-29（LTP curated ABI hole suite）
- **ゲート:** curated **679 PASS** / LTP curated **PASS n=38** / phase3 **ALL PASS** / desktop 意図しない ENOSYS **0**
- **LTP curated:** FS 基本 + process/wait（fork/exit/wait/waitpid/vfork/getppid）+ pipe/fd（dup2/dup3/fcntl/close/writev/readv/pread）+ signals（kill/sigaction/sigprocmask/alarm/SIGPIPE）；スモークは busybox AUTO_LOGIN
- **スタブ実体化:** `setitimer`/`getitimer`（alarm 共用）・`sched_getaffinity`；policy 94 は未着手のまま
- **wait 安定化:** ash `wait`（WNOHANG+sigsuspend）向け soft-zombie；`waitpid` は 1 子/呼出し；blocking `-1` は runnable へ yield；`rt_sigsuspend` 配線
- **phase3 ハーネス:** 後期 `cat|grep` / `$(pwd)` を回避（`true & wait $!` で waitall）
- **Desktop 本線:** W0–W3.5 + FB authority + Explorer。wm smoke GREEN
- **天井の壁（次へ回す）:** clone 本スレッド、`cat FILE|grep` 後期楔、双方向 TCP、コマンド置換 PF；LTP 次ラウンドは mmap／深い socket
- **Next:** clone THREAD 深化、または `cat|grep` 後期パイプライン修繕。Desktop 並行可
