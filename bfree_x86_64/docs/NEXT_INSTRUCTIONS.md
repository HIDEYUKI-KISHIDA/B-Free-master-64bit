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
BusyBox 実用ゲート（`tools/linux_box_gate.sh`）は **全 PASS で凍結**（CLI 深掘りは後回し）。ABI トラック A（A1–A7）は 2026-07-28 までで埋め済み（真のページ共有 CoW は未）。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-28
- **正本固定:** Desktop（C:）を唯一の作業ツリー。Native Qt GUI は `guest_main.cpp`（**W3.5** まで）。
- **BusyBox / phase3:** `phase3_guest_auto` **RESULT: ALL PASS**（2026-07-28）。`linux_box_gate` ash/pipe/persist/fork PASS；ping/nc はホスト echo タイムアウト（ABI 外）。
- **ABI トラック A（2026-07-28）:**
  - **A5** vfile mmap：R9 offset + **MAP_SHARED writeback**（munmap / `read()` 前に vf->data へ反映）。PRIVATE はコピーのまま
  - **A6/A7** `shm_open`/`shm_unlink` は vfile `shm/<name>` 配線済み（worklist を DONE に更新）。`memfd_create` も簡易 DONE（B3.3）
- **Practical Native OS D→E→F GREEN** (post A→B→C)
  - **D** FAT12/16 **RW** `/persist`: `tools/_f1_persist_fat_rw_smoke.sh` PASS; BFP1 `_f1_persist_smoke.sh` PASS
  - **E** Explorer (desk note) + Viewer + Terminal (1 cmd → `/persist/term.txt`); keys 1/2/3
  - **F** QML Rectangle soft SG try → explicit `SG fail->FB` (lookalike authority)
  - Smoke: `tools/_tmp_stage_abcd_smoke.sh`
- **Stabilize G0–G2 (pre–DesktopShell.qml):** dirty-only FB paint; no I/O in paint; Start launcher; QPA single input path; serial flood = FAIL. Host `DesktopShell.qml` / SG pixel authority **deferred**.
- **W0–W3.4:** 従来どおり。`g_w3_sg_pixels=0` FB 画素権威。
- **W3.5 window setVisible:** attach 末で `guest_w3_sync_one(0)` 経由の **outer 1 枚だけ** `setVisible(true)`（直接 Qt 呼び出しの肥大は早期 PF のため回避）。シリアル `W3.5 window setVisible ok`。Smoke: `tools/_tmp_qemu_wm_smoke.sh` KEY 全 PASS（含 **w35_setvisible** / no_panic）。
- **Also:** mmap session switches onto the 256MiB ctor stack (exec ~4MiB was overflowing QV4 after W1/W2 growth).
- **Next:** SG 画素権威（`g_w3_sg_pixels=1`）を安全に上げる、または host `DesktopShell.qml` 本読み込み。FAT32 / AHCI / 実機・BusyBox CLI / B1–B2 は後段。