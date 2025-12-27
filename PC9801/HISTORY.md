# B-Free PC-9801 Port: 5W1H と履歴

本ドキュメントは、`PC9801/` 配下のソース・ドキュメントに基づき、当時の開発意図（5W1H）と時系列の到達点を、根拠ファイルへのリンク付きで整理した記録です。

詳細な技術解説は付録 [APPENDIX.md](PC9801/APPENDIX.md) を参照してください。貢献者一覧は [CONTRIBUTORS.md](PC9801/CONTRIBUTORS.md) を、物語的な連関は [NARRATIVE.md](PC9801/NARRATIVE.md) を参照ください。

## 5W1H

### Who（誰が）
- B-Free Project（BTRON/ITRONベースのOSを開発）
- メンテナ・主要コミッタの痕跡:
  - Naitoh Ryuichi（night）: [PC9801/ChangeLog](PC9801/ChangeLog)
  - 2011年のCVSインポート実施者（`liu1` 名義）: 多数ファイルの `Initial Version.`

### What（何を）
- NEC PC-9801 実機向けに B-Free OS（BTRON/ITRON系）を「起動・入出力・ディスクI/O」まで持っていく初期ポート。
- マイクロカーネル（ITRON 3.0準拠）上に、メッセージ駆動のドライバ/サーバと、BTRON/POSIX環境を重ねる構成。
- 1st/2nd段のブートローダ、基本デバイス（コンソール/キーボード/タイマ/PIC/DMA/FD/SCSI）を実装し、対話的初期化（init）で検証。

### Why（なぜ）
- TRON系OS（BTRON/ITRON）の実機上での bring-up と研究・実装検証。
- BTRON API と POSIX 環境を併存させ、上位アプリの足場（プロセス・ファイル・IPC）を整える目的。
- 根拠: アーキ図・設計思想 [doc/introduction/architecture.txt](PC9801/doc/introduction/architecture.txt)、作業計画 [doc/introduction/worklist.tex](PC9801/doc/introduction/worklist.tex)、ステータス報告 [doc/note/kernel-status.text](PC9801/doc/note/kernel-status.text)

### When（いつ）
- 1994年: ブート設計メモ（BTRON/386 memo） [doc/note/boot.txt](PC9801/doc/note/boot.txt)
- 1995年: ChangeLog に初期版の流れ（FDドライバ追加、`init`追加、著作権表記） [PC9801/ChangeLog](PC9801/ChangeLog)
- 1996年: `init` 解析/入力処理の変更 [src/kernel/init/main.c](PC9801/src/kernel/init/main.c)
- 2011年: 既存資産のCVSインポート（`liu1` 名義の Initial Version 記録）

### Where（どこで/対象）
- 対象プラットフォーム: NEC PC-9801 アーキテクチャ（PC-98）。
- 対応は `PC9801/` 配下で進行（ブート・カーネル・ライブラリ・ツール・POSIX などの下位階層に分割）。

### How（どのように/どうやって）
- マイクロカーネル（ITRON 3.0準拠）: スケジューラ、IPC（メッセージバッファ）、タイマ、割込み、メモリ管理の基盤。
  - 仕様/思想: [doc/introduction/architecture.txt](PC9801/doc/introduction/architecture.txt)
- 2段ブート: 1st（8086/BIOSでFDからローダを展開）→ 2nd（32bit化/GDT/IDT/領域初期化/カーネル読込）。
  - 設計: [doc/note/boot.txt](PC9801/doc/note/boot.txt)
  - 実装: [src/boot/1st/1stboot.s](PC9801/src/boot/1st/1stboot.s), [src/boot/2nd](PC9801/src/boot/2nd)
- デバイスドライバ（メッセージ駆動）:
  - コンソール: テキストVRAMへ直接描画（スクロール/属性/漢字） [src/kernel/itron-3.0/common/console.c](PC9801/src/kernel/itron-3.0/common/console.c), [src/kernel/device/console](PC9801/src/kernel/device/console)
  - キーボード: 8251A系/割込み処理 [src/kernel/itron-3.0/pc9801/keyboard.c](PC9801/src/kernel/itron-3.0/pc9801/keyboard.c)
  - タイマ: PC-98特有I/Oポート（0x71/0x77） [src/kernel/itron-3.0/pc9801/timer.c](PC9801/src/kernel/itron-3.0/pc9801/timer.c)
  - PIC/DMA: 8259A/8237、1MB超DMA制御 [src/kernel/kernlib/interrupt.c](PC9801/src/kernel/kernlib/interrupt.c), [src/kernel/kernlib/dma.c](PC9801/src/kernel/kernlib/dma.c)
  - フロッピー: µPD765A [src/kernel/device/fd765a](PC9801/src/kernel/device/fd765a)
  - SCSI: WD33C93（パラメータ/パーティション/ブロックI/O） [src/kernel/device/wd33c93](PC9801/src/kernel/device/wd33c93)
  - グラフィックス基盤: GDC 7220/VRAMプレーン操作 [src/kernel/itron-3.0/pc9801/gdc7220.c](PC9801/src/kernel/itron-3.0/pc9801/gdc7220.c)
- 名前解決・IPCの統合: ポートマネージャ（`regist_port`/`find_port`）で「名前→ポートID」解決。ITRONのメッセージバッファ（`cre_mbf`/`snd_mbf`/`rcv_mbf`）でやり取り。
  - 設計: [doc/note/port-manager.text](PC9801/doc/note/port-manager.text)
  - 実装: [src/kernel/servers/port-manager.c](PC9801/src/kernel/servers/port-manager.c)
- 初期化（`init`）: デバイス発見・ポート確保・簡易シェル操作で検証。
  - 実装: [src/kernel/init/main.c](PC9801/src/kernel/init/main.c), [src/kernel/init/device.c](PC9801/src/kernel/init/device.c)
- POSIX 環境: ユーザAPI→トラップ→LOWLIB→サーバ（PM/FMなど）への橋渡し。DOS/BTRON FS へのブリッジ設計。
  - 設計: [doc/note/posix.text](PC9801/doc/note/posix.text)
  - 実装痕跡: [src/posix/usr/src/sys/server/PM](PC9801/src/posix/usr/src/sys/server/PM), [src/posix/usr/include/sys](PC9801/src/posix/usr/include/sys)

## タイムライン（抜粋）

- 1994年:
  - BTRON/386ブート設計メモ（2段ブート、メモリマップ、コマンド仕様） [doc/note/boot.txt](PC9801/doc/note/boot.txt)
- 1995年:
  - 09/21 v00.00.00 初版タグ作成 [PC9801/ChangeLog](PC9801/ChangeLog)
  - 09/22 著作権表示を整備 [PC9801/ChangeLog](PC9801/ChangeLog)
  - 10/03 FDドライバ追加 [PC9801/ChangeLog](PC9801/ChangeLog)
  - 10/11 `init` 追加 [PC9801/ChangeLog](PC9801/ChangeLog)
- 1996年:
  - `init` の入力解析等を改善 [src/kernel/init/main.c](PC9801/src/kernel/init/main.c)
- 2011年:
  - CVS由来ソースの一括インポート（`Initial Version.` 記録が多数）

## btron-pc 自動抽出年表（抜粋）

- 全体版: [PC9801/tools/btronpc_timeline.md](PC9801/tools/btronpc_timeline.md)
- 生成スクリプト: [PC9801/tools/extract_btronpc_timeline.ps1](PC9801/tools/extract_btronpc_timeline.ps1)
- 使い方（件数拡張可能）:
  - 例（年あたり5件）:
    - `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass; & "PC9801/tools/extract_btronpc_timeline.ps1" -MaxPerYear 5`
- 抜粋（例）:
  - 1991: btron-pcboot/2nd/boot.h, build_boot2.c, build_boot2_elf.c ...
  - 1992: btron-pckernel/BTRON/device/aha152x/...

## 到達点と未完の示唆

- 到達点:
  - PC-98 実機でのFDブート、テキストコンソール、KBD、タイマ、PIC/DMA、FDC、SCSI の動作。
  - `init` からデバイス操作・コンソール制御の確認。
  - 設計文書（アーキ/ポート管理/POSIX設計）が揃い、上位環境（BTRON/POSIX）への橋渡しが明確。
- 未完/後回し:
  - IDE/ATA ドライバは `config.tab` でコメントアウト（未統合） [src/kernel/make/config.tab](PC9801/src/kernel/make/config.tab)
  - RS-232C は存在するがビルド構成では無効化の記述あり [src/kernel/make/config.tab](PC9801/src/kernel/make/config.tab)
  - GDC 高度機能（解像度/プレーン最適化）・POSIX 全API/FS 完整備は道半ば。

## 代表的構成と根拠

- アーキテクチャ思想: [doc/introduction/architecture.txt](PC9801/doc/introduction/architecture.txt)
- 作業計画（work list）: [doc/introduction/worklist.tex](PC9801/doc/introduction/worklist.tex)
- ステータスレポート: [doc/note/kernel-status.text](PC9801/doc/note/kernel-status.text)
- ブート: [src/boot/1st/1stboot.s](PC9801/src/boot/1st/1stboot.s), [src/boot/2nd](PC9801/src/boot/2nd), [doc/note/boot.txt](PC9801/doc/note/boot.txt)
- デバイス: [src/kernel/device](PC9801/src/kernel/device)
- IPC/名前解決: [src/kernel/servers/port-manager.c](PC9801/src/kernel/servers/port-manager.c), [doc/note/port-manager.text](PC9801/doc/note/port-manager.text)
- POSIX: [doc/note/posix.text](PC9801/doc/note/posix.text), [src/posix](PC9801/src/posix)

---

更新の提案:
- 必要に応じ、デバイス単位の詳細（レジスタマップ・割込みフロー・制御コマンド）や、2ndブートのコマンド処理・SCSIパーティション解釈の「深掘り付録」セクションを追加します。
