# 📱 次の指示（スマホ編集用）

このファイルは **GitHub モバイルから直接編集して指示を残す**ためのものです。
次の作業セッション開始時に、エージェントはまずこのファイルを読みます。

## 使い方

1. GitHub アプリ / ブラウザでこのファイルを開く
2. ✏️（編集）→ 下の「指示」欄に書く → Commit changes
3. PC 側で次に Cursor を開いたら「NEXT_INSTRUCTIONS.md を見て」と言うだけ

## 指示

（ここに次にやってほしいことを書いてください）

- 例: F1 の FAT マウントまで進めて
- 例: phase3 が赤くなったら直して
- 例: 何もしない（保留）

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-22
- 完了: P0–P4, E1–E3, F1(ATA persist 実装+再起動永続 green), F2(UDP loopback), F3(LTP curated subset)
- 実行系: `tools/phase3_guest_auto.sh`（回帰）, `tools/_f1_persist_smoke.sh`, `tools/_f3_ltp_curated_smoke.sh`
- 選択肢（次の候補）:
  1. F1 続き: tiny FAT/ext2 で `/persist` を本マウント
  2. F2 続き: e1000 実 NIC / slirp 外向き UDP
  3. H01 fpstate (fxsave/fxrstor を rt_sigframe へ)
  4. timer 駆動 preemptive threads（E3 の preempt_disable を活用）
