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
本線: `work/posix-holes-redo`。**B1→B2→B3 ABI 完了** + **Desktop 本線 GREEN（W3.5 + QML ready hybrid）**。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-28
- **Phase5 (B1):** sticky-fork／waitall；SIGCHLD CATCH+restorer；SIGPIPE；seq-fork；NOFORK_ALL=0。`phase3` **ALL PASS**
- **Phase6 (B2):** mremap／membarrier／rseq；MAP_SHARED；`/musl_hello.elf`
- **B3:** epoll←timerfd；poll/eventfd 既存；socket は B4 方式
- **Desktop 本線（B3c）:** `QML ready (hybrid stack)` + W0–W3.5 + FB 画素権威。Smoke KEY: processEvents…w35 + **qml_ready** + start/wm + no_panic
- **ブロッカー 1:** `g_w3_sg_pixels=1` および attach 後 Quick 幾何 sync は早期 PF（CR2 可変）。host `DesktopShell.qml` 本読み込みは TypeCompiler hang 歴あり → **次の単独課題**
- **Next:** 安全な SG 画素権威（または DesktopShell 本読）を **単独** で掘る。FAT32／AHCI／futex 深化は後段。
