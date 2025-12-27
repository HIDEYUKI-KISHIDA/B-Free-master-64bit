## 図版強化（詳細補足）

### IRQ/割込みマップ（2nd段ブート文脈）

- PICポート定義: Master 0x00/0x02, Slave 0x08/0x0A（コマンド/データ）
  - 根拠: [interrupt.h](PC9801/src/boot/2nd/interrupt.h#L38-L42)
- 代表的ハンドラ（2nd）: `_int0_handler`～`_int16_handler`、SCSI割込み `_int35_handler`。
  - 根拠: [interrupt.s](PC9801/src/boot/2nd/interrupt.s#L29-L48), [SCSI handler](PC9801/src/boot/2nd/interrupt.s#L221)
- キーボード IDT 設定例: `set_idt (INT_KEYBOARD, ...)`（33h）。
  - 根拠: [keyboard.c](PC9801/src/boot/2nd/keyboard.c#L44)

### EOI（End Of Interrupt）の流れ

```
ISR → 本体処理 → PICへEOI書き込み(0x20) → 復帰

根拠: `movb $0x20, %al` による EOI 発行
      [interrupt.s](PC9801/src/boot/2nd/interrupt.s#L183),
      [interrupt.s](PC9801/src/boot/2nd/interrupt.s#L214),
      [interrupt.s](PC9801/src/boot/2nd/interrupt.s#L381)
```

### キーボード I/O ポート詳細（8251A系）

- 定義: `KEY_COM=0x43`, `KEY_STAT=0x43`, `KEY_DATA=0x41`
  - 根拠: [pc9801/keyboard.c](PC9801/src/kernel/itron-3.0/pc9801/keyboard.c#L28-L30),
          [2nd/location.h](PC9801/src/boot/2nd/location.h#L60-L62)
- 代表処理: 初期化 `outb(KEY_COM, 0x40)`、データ読み `inb(0x41)`
  - 根拠: [pc9801/keyboard.c](PC9801/src/kernel/itron-3.0/pc9801/keyboard.c#L201),
          [2nd/keyboard.c](PC9801/src/boot/2nd/keyboard.c#L96)

### GDC 7220 の分岐

- コマンド例: `outb (GDC_COMMAND, 0x49)`（描画モード制御）
  - 根拠: [console.c](PC9801/src/kernel/itron-3.0/common/console.c#L448)
- 実装: 640x400等のモード・プレーン操作
  - 根拠: [gdc7220.c](PC9801/src/kernel/itron-3.0/pc9801/gdc7220.c)

### 2ndブート `ls` の読み取りルート（概念）

```
入力解析 → パーティション解決 → ディレクトリエントリ列挙 → 表示

根拠: 2nd層の FS/ファイル操作: [file.c](PC9801/src/boot/2nd/file.c#L47)
```

### FDC (µPD765A) 状態遷移（概略）

```
コマンド構築 → 書き込み (FDC コマンドレジスタ) → `wait_int(&intr_flag)` で割込み待ち →
ISR によりフラグ更新 → ステータスブロック読み出し → 次段（再試行/完了/エラー）

根拠: [fdc.c](PC9801/src/kernel/device/fd765a/fdc.c#L428), [fdc.c](PC9801/src/kernel/device/fd765a/fdc.c#L490)
```

### SCSI (WD33C93) 処理フロー（概略）

```
要求生成（READ/WRITE/INQUIRY 等）→ WD33C93 へコマンド発行 → ステータス取得 →
必要に応じてデータ転送・リトライ → パラメータ/パーティション更新 → 応答返却

根拠: [scsi.c](PC9801/src/kernel/device/wd33c93/scsi.c#L156),
      [main.c](PC9801/src/kernel/device/wd33c93/main.c#L221)
```

### GDC 7220 代表コマンド例（抜粋）

- 0x49: 描画/モード系コマンドの一例（コンソール描画制御）
  - 根拠: [console.c](PC9801/src/kernel/itron-3.0/common/console.c#L448)
- そのほかの詳細は、プレーン/モード設定処理を含む [gdc7220.c](PC9801/src/kernel/itron-3.0/pc9801/gdc7220.c) を参照
# B-Free PC-9801 Port: 技術付録（Deep Dive）

本付録は、[HISTORY.md](PC9801/HISTORY.md) を補完する技術解説です。実装の意図や構造を、該当ソースと設計文書への根拠リンクと併せて記述します。

## ブート詳細

- 概要: 1st/2nd の二段ブート。1st は 8086/BIOS でFDからローダを読み、2nd が 32bit 化・GDT/IDT 設定・領域初期化・カーネル読込を行う。
  - 設計: [doc/note/boot.txt](PC9801/doc/note/boot.txt)
  - 1st 実装: [src/boot/1st/1stboot.s](PC9801/src/boot/1st/1stboot.s#L239-L248)（`readdisk` 呼び出しなど）
  - 2nd 実装: [src/boot/2nd/file.c](PC9801/src/boot/2nd/file.c#L47)（FSヘッダ識別）, [src/boot/2nd/main.c](PC9801/src/boot/2nd/main.c#L627-L628)（ヘッダ出力）
- 2nd の対話コマンド（例）: `ls [partition] [path]`, `boot [partition] [path]`, `reset`（詳細: [doc/note/boot.txt](PC9801/doc/note/boot.txt)）
- パーティション指定: `FD:0`（AドライブFD）, `SCSI0:0`（SCSI ID0/LUN0）等（同上）
- メモリマップ（概念）: 16bit/32bit切り替え、GDT/IDT 配置など（同上）。

## デバイス実装（PC-98固有点）

- コンソール（テキストVRAM直書き）
  - VRAMベース: [src/kernel/itron-3.0/common/console.c](PC9801/src/kernel/itron-3.0/common/console.c#L35) / [src/kernel/device/console/misc.c](PC9801/src/kernel/device/console/misc.c#L43)
  - 機能: スクロール・属性・漢字描画、行末処理、クリア等
  - 代表処理: `write_vram()`/`write_kanji_vram()`（前掲ファイル参照）

- キーボード（8251A/割込み33h）
  - 割込み: [pc9801/pc98.h](PC9801/src/kernel/itron-3.0/pc9801/pc98.h#L27)
  - 実装: [pc9801/keyboard.c](PC9801/src/kernel/itron-3.0/pc9801/keyboard.c)（`int33_handler`設定・メッセージ配送）

- タイマ（I/Oポート 0x71/0x77）
  - 実装: [pc9801/timer.c](PC9801/src/kernel/itron-3.0/pc9801/timer.c#L23-L25)
  - ソフトタイマキュー・トリガ処理を実装

- PIC（8259A）/DMA（8237+拡張）
  - PIC ポート: [pc98.h](PC9801/src/kernel/itron-3.0/pc9801/pc98.h#L20-L24)
  - DMA 拡張制御: [kernlib/dma.c](PC9801/src/kernel/kernlib/dma.c#L76)（1MB越えアクセス制御レジスタ `0x439`）

- フロッピーディスク（µPD765A）
  - 実装: [device/fd765a/fdc.c](PC9801/src/kernel/device/fd765a/fdc.c#L45), [device/fd765a/fdc.c](PC9801/src/kernel/device/fd765a/fdc.c#L136)
  - busywait/割込み待ち、各種ステータス判定、ヘッド/シリンダ/セクタ管理

- SCSI（WD33C93）
  - 実装: [device/wd33c93/main.c](PC9801/src/kernel/device/wd33c93/main.c#L30)（`disk_table`）、[device/wd33c93/main.c](PC9801/src/kernel/device/wd33c93/main.c#L221)（`print_disk_param`）
  - ステータス取得: [device/wd33c93/scsi.c](PC9801/src/kernel/device/wd33c93/scsi.c#L156)
  - パーティション解釈・ブロックI/O（同ファイル群）

- グラフィックス基盤（GDC 7220/VRAMプレーン）
  - 実装: [pc9801/gdc7220.c](PC9801/src/kernel/itron-3.0/pc9801/gdc7220.c)
  - プレーン毎のアドレス計算・ビット演算

- 未統合/任意機能
  - IDE/ATA: `config.tab` でコメントアウト [src/kernel/make/config.tab](PC9801/src/kernel/make/config.tab#L30)
  - RS-232C: 行はあるが既定無効 [src/kernel/make/config.tab](PC9801/src/kernel/make/config.tab#L32)

## IPC と名前解決（ポートマネージャ）

- 目的: 「名前→ポートID」解決を一元化し、ドライバ/サーバを疎結合に。
- 設計: [doc/note/port-manager.text](PC9801/doc/note/port-manager.text)
- 実装: [src/kernel/servers/port-manager.c](PC9801/src/kernel/servers/port-manager.c)
- メッセージ構造（設計書）
  - ヘッダ: `type`（`REGIST_PORT`/`UNREGIST_PORT`/`FIND_PORT`）と `size`
  - ボディ: `regist_port_t { name, port, task }`, `unregist_port_t { name, task }`, `find_port_t { name }`
  - ライブラリAPI例: `regist_port()`, `unregist_port()`, `find_port()`（実使用は `init/device.c` からの `find_port()` 等を参照）

## POSIX ブリッジ（LOWLIB/サーバ）

- 目的: BTRON上にPOSIX互換APIを提供し、プロセス/シグナル/ファイルI/Oを整備。
- 設計: [doc/note/posix.text](PC9801/doc/note/posix.text)
- 構成: ユーザAPI → トラップ（例: `trap #65`）→ LOWLIB → サーバ（PM/FM 等）→ ドライバ/FS
- 実装痕跡: PM（プロセスマネージャ） [src/posix/usr/src/sys/server/PM](PC9801/src/posix/usr/src/sys/server/PM)

## 初期化プログラム（init）

- 役割: 起動後のデバイス発見・ポート確保・簡易対話（`init>`）
- 実装: [src/kernel/init/main.c](PC9801/src/kernel/init/main.c), [src/kernel/init/device.c](PC9801/src/kernel/init/device.c)
- 例: コンソールクリア `DEV_CTL` 送信 [device.c](PC9801/src/kernel/init/device.c#L63-L95)

## 貢献者とログの痕跡

- 主要コミッタ:
  - Naitoh Ryuichi（`night`）— 1995年前後の初期ポート: [ChangeLog](PC9801/ChangeLog)
  - `liu1` — 2011年に CVS 由来の `Initial Version.` をインポート（多くのファイル先頭の `$Header`/`$Id`）

---

補足のご要望に応じて、各デバイスの割込みフロー図、2ndブートのコマンド処理シーケンス図、POSIXの `fork`/シグナル経路のトレース図なども追加可能です。

---

## 割込みフロー図（抜粋）

キーボード（INT 33）

```
IRQ(33h) → IDT登録 [pc9801/keyboard.c#L195] → `int33_handler` → メッセージバッファ `ITRON_KEYBOARD_MBF` へ投入 →
`keyboard_task` が `rcv_mbf` → 上位へキーイベント配送

根拠: [pc9801/keyboard.c](PC9801/src/kernel/itron-3.0/pc9801/keyboard.c#L181-L221)
```

タイマ（INT 32）

```
PIT(0x71/0x77) 設定 → IRQ(32) → タイマ割込み → タイマキューを評価 → 満了コールバック `set_timer` 登録先を起床

根拠: [pc9801/timer.c](PC9801/src/kernel/itron-3.0/pc9801/timer.c#L23-L43)
```

FDC（フロッピー）

```
コマンド発行 → `wait_int(&intr_flag)` で割込み待ち → ISRでフラグ更新 → ステータス読み出し/次段処理

根拠: [fdc.c](PC9801/src/kernel/device/fd765a/fdc.c#L428), [fdc.c](PC9801/src/kernel/device/fd765a/fdc.c#L490)
```

PIC（8259A）マスク制御

```
有効化: `outb(MASTER_8259A_DATA, inb(..) & ~(1<<n))` → スレーブ側は (n-8) を使用

根拠: [kernlib/interrupt.c](PC9801/src/kernel/kernlib/interrupt.c#L38-L40)
```

## 主要レジスタ表（PC-98）

| 機能 | アドレス/定義 | 備考 | 根拠 |
|---|---|---|---|
| PIC Master CMD/DATA | 0x00 / 0x02 | マスク制御 | [pc98.h](PC9801/src/kernel/itron-3.0/pc9801/pc98.h#L20-L21) |
| PIC Slave CMD/DATA  | 0x08 / 0x0A | スレーブ | [pc98.h](PC9801/src/kernel/itron-3.0/pc9801/pc98.h#L23-L24) |
| IRQ ベクタ          | INT_TIMER=32, INT_KEYBOARD=33 | 固定割付 | [pc98.h](PC9801/src/kernel/itron-3.0/pc9801/pc98.h#L26-L27) |
| タイマ              | 0x71 (READ/WRITE), 0x77 (CTRL) | PC-98固有 | [timer.c](PC9801/src/kernel/itron-3.0/pc9801/timer.c#L23-L25) |
| DMA 1MB超制御       | 0x439 | DMA over 1MB | [dma.c](PC9801/src/kernel/kernlib/dma.c#L76) |
| テキストVRAM        | 0x800A0000 | 80×25相当 | [console.c](PC9801/src/kernel/itron-3.0/common/console.c#L35) |
| VRAMメモリ範囲      | 0xA0000–0xFFFFF | 物理側参照 | [pc98.h](PC9801/src/kernel/itron-3.0/pc9801/pc98.h#L37-L38) |

注意: 実装はI/O抽象化や機種差分の影響を受けるため、機種により差異がありえます。詳細は各ドライバ実装を参照してください。

## 2ndブート コマンド処理シーケンス（概略）

`boot [partition] [path]` の例

```
入力解析 → パーティション解決（例: FD:0 / SCSI0:0） → ファイル先頭ヘッダ確認（OS/DISK ID） →
読込・配置 → カーネルへ制御移譲

根拠: [file.c](PC9801/src/boot/2nd/file.c#L47), [main.c](PC9801/src/boot/2nd/main.c#L627-L628), 仕様: [boot.txt](PC9801/doc/note/boot.txt)
```

`ls [partition] [path]` は、指定ディレクトリのエントリ列挙（実装詳細は2ndのFS層を参照）。

## POSIX `fork`/シグナル 経路（設計ベース）

```
ユーザAPI (fork/signal) → Trap (#65等) → LOWLIB → ITRON呼び出し / 名前解決 →
PM（プロセスマネージャ）/FM（ファイル）サーバへ要求 → 応答をLOWLIBで整形 → ユーザ空間へ返却

補足: fork時は vcre_reg/vmap_reg 等の仮想領域操作、プロセスIDの払い出し、スタック/エントリの設定が関与。
根拠: 設計 [posix.text](PC9801/doc/note/posix.text), 実装痕跡 [PM](PC9801/src/posix/usr/src/sys/server/PM)
```

