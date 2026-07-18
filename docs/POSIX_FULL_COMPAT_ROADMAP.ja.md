# B-Free x86_64 — Linux/POSIX 完全互換トラッキング

> **目的:** BusyBox / musl ゲストが「通常の Linux CLI」として動く状態を経て、最終的に Linux ABI / POSIX ユーザーランド互換を目指す。  
> **方針:** 一発実装ではなく、検証可能な小さな PR を積み上げる。未到達は `ENOSYS` で明示する。

## 現状（2026-07）

ゲスト（`bfree_x86_64`）は協調的な単一タスク + 合成 FS 上で BusyBox ash の回帰（Phase 3 / Phase 4 基盤）を通している。

既に入っているもの（ローカル作業ツリー）:

- `/tmp` 階層 vnode、独立 open-file description
- 1 子スロットの `vfork` →（可能なら）子 AS への `execve` → `exit` → `wait4`
- `fork` / 非 VFORK `clone` / `waitid` は当面 `ENOSYS`（嘘の成功を返さない）

明示 `-ENOSYS` は個別に数本 + **未登録 Linux syscall の default**。Linux x86_64 の syscall は数百本あり、dispatch に載っているのはおおよそ 100 本弱で、その多くも意味論が狭いスタブである。

## マイルストーン（PR 単位の積み上げ）

### M0 — トラッキング（本 PR）

- ゴールと非ゴールの固定
- 以降の PR はこの文書のチェックリストを更新する

### M1 — ファイルシステム（Phase 4A–D） ✅ 完了

- [x] 独立 OFD（別 `open` = 別オフセット、`dup` = 共有）
- [x] `/tmp` 実ディレクトリ（mkdir/rmdir/ネスト/chdir）
- [x] dirent カーソルをすべて OFD へ（`kernel/sysmain/fs_ofd.c`、検証 `P4_DIRENT_OFD`）
- [x] unlink-while-open / vnode 寿命（`nref` + `unlinked`、検証 `P4_UNLINK_OPEN`）
- [x] `openat`/`*at` の dirfd 相対解決（`openat`/`mkdirat`/`unlinkat`、検証 `P4_OPENAT`）
- [x] 永続ブロック FS（`blk_vol.c`/`blk_persist.c`、検証 `P4_BLOCK_FS`）

### M2 — プロセス（最小の正直なモデル）

- [x] 1 子・child-first `vfork`
- [x] 子専用ページテーブルへの `execve`（失敗時フォールバックあり）
- [x] `fork` = `ENOSYS`（コピー無しを偽らない）
- [ ] BusyBox NOFORK-all 緩和 → 本物の applet reexec 回帰
- [ ] 複数 zombie / 正しい `waitid`
- [ ] eager-copy または COW の `fork`
- [ ] プリエンプティブな複数 runnable + パイプ両端同時実行
- [ ] シグナル配送（SIGCHLD / SIGINT / SIGPIPE / ジョブ制御）

### M3 — 通常 CLI（BusyBox パッチ巻き戻し）

- [ ] inproc pipe / bg-inline の撤去（M2 の複数子が前提）
- [ ] NOFORK-all 撤去、upstream NOFORK のみ
- [ ] 外部コマンド・コマンド置換・サブシェルの回帰

### M4 — musl / 一般静的バイナリ

- [ ] 主要 syscall の意味論を stub → 実実装へ
- [ ] スレッド（`clone` スレッドフラグ）、robust futex
- [ ] tty ジョブ制御、セッション / プロセスグループ

### M5 — POSIX / Linux 完全互換（最終 PR 群）

- [ ] ネット・IPC・権限・マウント・デバイスノード
- [ ] 未実装 syscall の `ENOSYS` を意図的 residual 以外ゼロへ
- [ ] POSIX テストスイート / LTP のゲート（範囲は別途定義）

## 非ゴール（このトラックでは約束しない）

- 一 PR での「POSIX 全部」
- ホスト Linux とビット単位で同じ性能・同じ FS レイアウト
- すべての Linux 拡張（io_uring 全機能、名前空間完全互換など）を初期から揃えること

## ENOSYS 方針

| 種別 | 扱い |
|------|------|
| 未実装 syscall | `ENOSYS`（default） |
| 部分実装（例: VFORK なし clone） | `ENOSYS` または `EINVAL` |
| 互換のため嘘の成功 | **禁止**（過去の罠） |

`fork` を協調 `vfork` に戻すのは「互換の見た目」であり POSIX fork ではない。完全互換トラックでは **本物の fork が来るまで ENOSYS を維持**する。

## 検証ゲート

各マイルストーン PR は少なくとも次を通す:

1. `tools/phase3_guest_auto.sh` → `RESULT: ALL PASS`
2. その PR が追加した専用マーカー（例: `P4_*`）
3. カーネル panic / 連続 GP 無し

## 関連ローカルパス

実装の主戦場:

- `bfree_x86_64/kernel/sysmain/fs_ofd.c`
- `bfree_x86_64/kernel/sysmain/blk_vol.c`
- `bfree_x86_64/kernel/sysmain/blk_persist.c`
- `bfree_x86_64/kernel/sysmain/syscall.c`
- `bfree_x86_64/kernel/sysmain/process.c`
- `bfree_x86_64/kernel/sysmain/vmm.c`
- `bfree_x86_64/tools/phase3_guest_auto.sh`
- `bfree_x86_64/tools/build_guest_busybox.sh`
