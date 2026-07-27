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
本線: `work/posix-holes-redo`。**desktop / QML ready** 優先。
BusyBox 実用ゲート（`tools/linux_box_gate.sh`）は **全 PASS で凍結**（CLI 深掘りは後回し）。ABI 最小穴（A2/A1/A5）は 2026-07-27 スプリントで埋め済み。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-27
- **正本固定:** Desktop（C:）を唯一の作業ツリーとした。Native Qt GUI は `userland/desktop_qt/guest_main.cpp`（**W3.4** まで）。
- **BusyBox:** `linux_box_gate` ash/pipe/persist/fork PASS；ping/nc はホスト echo タイムアウトで FAIL（ABI 外）。`phase3_guest_auto` **RESULT: ALL PASS**。
- **ABI sprint (2026-07-27):**
  - **A2** non-VFORK/non-THREAD `clone` → AS-copy fork（ENOSYS 解消）
  - **A1** zombie soft-reap で fork PT 解放（sticky EAGAIN 防止）；partial DONE
  - **A5** vfile mmap が R9 offset/pgoff を尊重；partial DONE（MAP_SHARED 未）
- **Practical Native OS D→E→F GREEN** (post A→B→C)
  - **D** FAT12/16 **RW** `/persist`: `tools/_f1_persist_fat_rw_smoke.sh` PASS; BFP1 `_f1_persist_smoke.sh` PASS
  - **E** Explorer (desk note) + Viewer + Terminal (1 cmd → `/persist/term.txt`); keys 1/2/3
  - **F** QML Rectangle soft SG try → explicit `SG fail->FB` (lookalike authority)
  - Smoke: `tools/_tmp_stage_abcd_smoke.sh`
- **Stabilize G0–G2 (pre–DesktopShell.qml):** dirty-only FB paint; no I/O in paint; Start launcher; QPA single input path; serial flood = FAIL. Host `DesktopShell.qml` / SG pixel authority **deferred**.
- **W0–W3.3:** 従来どおり（FB mini-WM + Quick chrome probes）。Smoke KEY PASS。
- **W3.4 sustained SG:** W3.2 soft UpdateRequest を権威に、attach 末で `W3.4 sustained SG ok`（追加 UpdateRequest / setVisible は PF のため見送り）。`g_w32_sg_taskbar=0` FB 画素権威。Smoke: `tools/_tmp_qemu_wm_smoke.sh` KEY 全 PASS（含 w34）。
- **Also:** mmap session switches onto the 256MiB ctor stack (exec ~4MiB was overflowing QV4 after W1/W2 growth).
- **Next:** window Quick `setVisible` 再試行（QQmlEngine PF 回避）、または sustained soft UpdateRequest を安全に再開。FAT32 / AHCI / 実機は後段。BusyBox CLI 深掘りは再凍結。