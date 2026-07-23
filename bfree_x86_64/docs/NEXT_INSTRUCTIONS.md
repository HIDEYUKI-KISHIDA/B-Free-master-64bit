# 📱 次の指示（スマホ編集用）

このファイルは **GitHub モバイルから直接編集して指示を残す**ためのものです。
次の作業セッション開始時に、エージェントはまずこのファイルを読みます。

## 使い方

1. GitHub アプリ / ブラウザでこのファイルを開く
2. ✏️（編集）→ 下の「指示」欄に書く → Commit changes
3. PC 側で次に Cursor を開いたら「NEXT_INSTRUCTIONS.md を見て」と言うだけ

## 指示

本線: `work/posix-holes-redo`。Polish→Phase7。
進捗 2026-07-23: E1–E3 / F1 / F2 UDP+TCP / F3 / pthread-clone / FAT BPB probe scaffold。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-23
- F1: ATA PIO + `/persist` BFP1 — `_f1_persist_smoke.sh`
- F1b: FAT BPB probe（`BFREE_PERSIST_FAT_PROBE=1`）— `_f1_persist_fat_probe_smoke.sh`；本マウントは未
- F2: UDP TX/RX + **外向き TCP**（`_f2_e1000_tcp_smoke.sh` green）
- F3: LTP curated subset
- gthr / pthread→clone；desktop.elf relink済み
- 次: FAT 本マウント（read-only から）/ desktop 本線回帰
