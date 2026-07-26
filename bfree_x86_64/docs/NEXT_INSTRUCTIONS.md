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

次へ（W3.2 の次）

<!-- PHONE_END -->

書き方の例:
- `次へ`
- `W3.3 で sustained SG を試せ`
- `止まって。スモークだけ回せ`
- `ISO を作り直して`

---

## 常設方針（エージェント用・スマホでは触らなくてよい）

本線: `work/posix-holes-redo`。**desktop / QML ready** 優先。
BusyBox 実用ゲート（`tools/linux_box_gate.sh`）は **全 PASS で凍結**（CLI 深掘りは後回し）。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-27
- **BusyBox:** `linux_box_gate` ALL PASS。凍結。
- **Practical Native OS D→E→F GREEN** (post A→B→C)
  - **D** FAT12/16 **RW** `/persist`: `tools/_f1_persist_fat_rw_smoke.sh` PASS; BFP1 `_f1_persist_smoke.sh` PASS
  - **E** Explorer (desk note) + Viewer + Terminal (1 cmd → `/persist/term.txt`); keys 1/2/3
  - **F** QML Rectangle soft SG try → explicit `SG fail->FB` (lookalike authority)
  - Smoke: `tools/_tmp_stage_abcd_smoke.sh`
- **Stabilize G0–G2 (pre–DesktopShell.qml):** dirty-only FB paint; no I/O in paint; Start launcher; QPA single input path; serial flood = FAIL. Host `DesktopShell.qml` / SG pixel authority **deferred**.
- **W0 mini-WM:** multi-window `{x,y,w,h,z}`; title drag; SE resize; X/Esc close; taskbar slots. Smoke: `wm drag` / `wm resize`.
- **W0.1 interactive:** larger X / edge resize hits; Start menu Restart/Shutdown/Sleep stubs; idle paint throttle (CPU FB only — GPU/SG not pixel authority yet). View ISO: `~/bfree-stage/bfree-stage-abcd.iso` (`-machine pc,usb=off`).
- **W0.2 chrome:** KWin-like FB frames (border/shadow/client inset); cursor-only idle paint; Sleep overlay (key wake); Restart/Shutdown = CPU halt banner (no ACPI yet). Bridge-only mouse (skip WSI hang).
- **W1 taskbar polish:** minimize + task raise/minimize click; host-like Start/Search/clock/hairline; focus underline; `W1 taskbar ready`. Still FB lookalike (no DesktopShell.qml yet).
- **W2 window layer (FB parity):** title min/max/close; maximize fill desk + restore rect; W/E/S/SE resize; title dbl-click maximize; `W2 window layer ready`. Still FB mini-WM (not full DesktopShell.qml).
- **W3 Quick window chrome:** C++ sparse `QQuickRectangle` tree (no `QQuickText`) matching host `desktopWindowLayer` slate, built once under `contentItem` at attach and kept invisible; soft SG breadcrumb without update/expose; no event-loop Quick mutates (those GPF'd input); FB remains pixel authority (`g_w3_sg_pixels=0`) with host-slate window paint; `W3 window layer ready`. Full DesktopShell.qml still not loaded. Smoke KEY all PASS.
- **W3.1 Start/taskbar Quick slice:** attach-once invisible C++ `QQuickRectangle` taskbar + Start panel (host colors); FB Start menu enlarged toward host `launcherPanel` (`#111827`, search strip, rows, power footer); hit geometry shared; `W3.1 start/taskbar layer ready`. Still FB pixel authority.
- **W3.2 Sparse SG taskbar probe:** attach-time show taskbar Quick **bar** + soft UpdateRequest (no force-expose drain — that PFs); `g_w32_sg_taskbar=0` so FB still paints strip; Start/windows FB; `W3.2 SG probe ok` / `W3.2 SG taskbar pixels`. No event-loop Quick mutates.
- **Also:** mmap session switches onto the 256MiB ctor stack (exec ~4MiB was overflowing QV4 after W1/W2 growth).
- **Next:** Sustained SG expose / window Quick pixels (still no full DesktopShell.qml). FAT32 / AHCI / 実機は後段。BusyBox CLI 深掘りは凍結のまま。
