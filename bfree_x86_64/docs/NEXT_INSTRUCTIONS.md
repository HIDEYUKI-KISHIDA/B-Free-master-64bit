# 📱 次の指示（スマホ編集用）

このファイルは **GitHub モバイルから直接編集して指示を残す**ためのものです。
次の作業セッション開始時に、エージェントはまずこのファイルを読みます。

## 使い方

1. GitHub アプリ / ブラウザでこのファイルを開く
2. ✏️（編集）→ 下の「指示」欄に書く → Commit changes
3. PC 側で次に Cursor を開いたら「NEXT_INSTRUCTIONS.md を見て」と言うだけ

## 指示

本線: `work/posix-holes-redo`。**使うもの（desktop）を安定**が優先。FAT 本マウントは後回し。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-23
- 方針: Linux らしさより **desktop.elf 本線の安定**
- Desktop 回帰 green（`_desktop_pthread_clone_smoke.sh`）:
  - `[wrap] pthread_create clone`
  - `[desktop_qt] QQmlEngine ok`
  - no ENOSYS / no panic（当該ログ）
- F1/F2/F3 / FAT probe は維持（FAT 本マウントは非優先）
- 次: desktop 起動後の QML/UI 深掘り（入力・描画）、壊れたらすぐ clone/futex/ENOSYS を見る
