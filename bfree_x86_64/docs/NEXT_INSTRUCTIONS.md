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
本線: `work/posix-holes-redo`。**実用 Desktop = FB 画素権威**（W3.5 hybrid）。SG／DesktopShell 本読は別トラック。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-07-28
- **Desktop 本線:** W0–W3.5 + FB authority + Explorer 実用。wm smoke GREEN
- **musl libc-test サブセット:** **261 TPASS**（純 libc 拡張 + 実 `fork`+`waitpid`）
- **穴埋め（本ラウンド）:** Phase1 純 libc → Phase2 fork PF 修正
  - `exit_from_fork`: wait 完了を mode-2 + status/reap、親 PT を強制復帰、wait 後は SIGCHLD pending を落とす
  - curated `proc_fork_wait` を実 `fork`/`waitpid`/`WEXITSTATUS==42` に復帰
- **天井（どこまで広げられるか）:**
  - **まだ伸ばせる:** 純 libc、既存 stub の組み合わせ、UDP/UNIX の浅いケース
  - **すぐ壁:** clone スレッド、双方向 TCP 深化、フル POSIX ファイル属性、pipe/UNIX スロット枯渇、ash vfork 親 PF（スイート後・スモークは無視）
  - **本セット一括:** まだ不可。curated は「穴発見用サブセット」が役割
- **既知ノイズ:** スイート PASS 後の ash vfork 親 PF。スモークは RESULT PASS 後を無視
- **保留:** ash vfork 親 PF 本線（スイート外）
- **Next:** 天井までさらに広げるか、ash vfork 親 PF。Desktop は並行可
