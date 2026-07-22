# 📱 次の指示（スマホ編集用）

このファイルは **GitHub モバイルから直接編集して指示を残す**ためのものです。
次の作業セッション開始時に、エージェントはまずこのファイルを読みます。

## 使い方

1. GitHub アプリ / ブラウザでこのファイルを開く
2. ✏️（編集）→ 下の「指示」欄に書く → Commit changes
3. PC 側で次に Cursor を開いたら「NEXT_INSTRUCTIONS.md を見て」と言うだけ

## 指示

本線: `work/posix-holes-redo`。Polish→Phase7。
進捗 2026-07-22: E1–E3 / F1 / **F2 e1000 TX** / F3 green。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-22
- F1: ATA PIO + `/persist` BFP1 — `_f1_persist_smoke.sh`
- F2: loopback + e1000 `sendto`→`udp_send` — `_f2_e1000_udp_smoke.sh` → `F2_E1000_TX_OK`
- F3: LTP curated subset
- gthr wait/wake PASS（カーネル差分に含む）
- 次: RX echo / slirp 外向き、または desktop.elf 本線
