# 📱 次の指示（スマホ編集用）

このファイルは **GitHub モバイルから直接編集して指示を残す**ためのものです。
次の作業セッション開始時に、エージェントはまずこのファイルを読みます。

## 使い方

1. GitHub アプリ / ブラウザでこのファイルを開く
2. ✏️（編集）→ 下の「指示」欄に書く → Commit changes
3. PC 側で次に Cursor を開いたら「NEXT_INSTRUCTIONS.md を見て」と言うだけ

## 指示

本線: `work/posix-holes-redo`。Polish→Phase7。
進捗 2026-07-22: E1–E3 / F1 / **F2 e1000 TX+RX echo** / F3 green。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-22
- F1: ATA PIO + `/persist` BFP1 — `_f1_persist_smoke.sh`
- F2: loopback + e1000 TX (`_f2_e1000_udp_smoke.sh`) + **RX/echo** (`_f2_e1000_udp_rx_smoke.sh` → hostfwd PING/PONG)
- F3: LTP curated subset
- gthr wait/wake PASS；**pthread→clone** wrap；smoke `_pthread_clone_smoke.sh`；**desktop.elf relink + 起動で `[wrap] pthread_create clone` 確認**（`_desktop_pthread_clone_smoke.sh`）
- 次: slirp 外向き TCP / desktop 本線回帰
