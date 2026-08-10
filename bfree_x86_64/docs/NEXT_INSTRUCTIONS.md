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

- **Updated:** 2026-08-10（**Deep follow-on 1→2→3 完了**）
- **Deep1（QML RIP=0）:** 根因＝activate 後の sync `UpdateRequest` 塗装。配送 OFF で **`setX` 解禁** → `RESULT=GREEN qml-mainline-1-4-deep1` / PF=0（`post-act termX place ok`）。dense UR / setVisible は配送 ON のまま据え置き
- **Deep2（lazy COW）:** fork 時 RW ページを共有 PTE（RO+COW bit）→ write #PF で copy；`[COW] break` 観測；**`LTP_CURATED_RESULT: PASS n=240`** + f3 に COW ゲート
- **Deep3（e1000）:** `_f2_e1000_tcp_smoke.sh` — 3RTT 後 close → **第2接続** + RTT4 緑（CONNECT2/RTT4）
- **① Desktop ENOSYS:** 維持（unique=0）
- **据え置き（さらに）:** post-activate dense UR を安全に ON；COW 参照カウント/解放；TCP 窓・ロス再送の意図的試験
- **Next:** スマホ指示待ち

---
---

