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
- `止まって。スモークだけ回せ`
- `ISO を作り直して`

---

## 常設方針（エージェント用・スマホでは触らなくてよい）

**正本:** `C:\Users\h_kis\Desktop\B-Free-master`（詳細: `docs/CANONICAL.md`）。`J:\B-Free-master` は古いコピー・使わない。
本線: `work/posix-holes-redo`。

**優先: ホスト DesktopShell 必須の 3 穴。** 薄い塊寄せは据え置き。

| # | 穴 | 状態（強化プローブ 2026-08-08） |
|---|-----|------|
| H1 | Item↔Item `setParentItem` | **緑**（QML↔QML / bare Item / nested）— 製品ツリー解禁候補 |
| H2 | SG flush | 単純 `UpdateRequest` **緑**；**H2b 緑**（プロトコル＋ Quick nullkids） |
| H3 | kde/wabi 子 IR | **緑**（ClockApplet create+parent；下記） |

**着手順（残り）:** 必須 8 + 拡張 4（ホスト権威 1〜12）は 2026-08-08 本線スモーク緑。相対 CU `addImport` は **H3 URL bypass**（soft-rel はハングで据え置き）。

**H2b:** unexpose → contentItem 兄弟を hidden で attach → re-expose → leaf update → `requestUpdate`（hidden）→ show → `requestUpdate`。

**H3 根因:** 古い `guest_clock_applet_qmlcache` が HIT 直後 `QString::replaceArgEscapes` で PF@CR2=0x29000000。URL 自体は無害（Child ユニットを Clock URL に載せると緑）。修正: `ClockApplet.qml` を `import QtQuick`（**バージョン無し**；`2.15` 付き極小ユニットは赤）で再 qmlcachegen + `install_guest_qmlcache_unit.py --ref guest_product_child_qmlcache.cpp`（`tools/_tmp_rebuild_clock_qmlcache.sh`）。Timer / Date / formatDateTime 付き本番相当で緑。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-08-10（**① Desktop ENOSYS 再計測＋ゲート化 / ② 製品スモーク**）
- **① Desktop ENOSYS:** `tools/_desktop_enosys_smoke.sh` → `DESKTOP_ENOSYS_RESULT: PASS unique=0 transfer=1 qml_ready=1`（埋める nr なし；証拠 `out/desktop_enosys/`）
- **① ゲート:** 同上スクリプトが unique≠0 または transfer なしで exit 1；`guest_desktop_smoke.sh` もシリアルに ENOSYS 集計を追加し unique≠0 なら FAIL
- **② LTP curated:** 維持緑 `PASS n=238 fail=0`（前回 S+B）
- **据え置き / Next slices:**
  - 真 CoW SHARED: 次スライス＝同一物理ページ map→write COW break 最小プローブ
  - LAN TCP e1000: 次スライス＝tcp_min 1シナリオ緑拡大
  - 本番 QML post-activate PF: ABI外・本線ログで PF1件収束
- **Next:** QML 本線 or CoW slice

---
---

