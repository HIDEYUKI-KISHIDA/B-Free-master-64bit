# B-Free PC-9801 開発史（物語）と btron-pc への接続

本稿は、事実年表だけでなく「なぜ・どうつながったか」を物語的にまとめ、PC-98 ポートが btron-pc の系譜へどう橋渡ししたかを記録します。

## はじまり（1994–1995）
- 背景: 当時の国内実機として PC-98 は圧倒的に普及。TRON 系 OS を「現実のマシンで動かす」ための最短距離が PC-98 でした。
- 目標: ITRON 3.0 を土台に、割込み・タイマ・メモリ・IPC・デバイスを最小実装し、上位に BTRON/POSIX を載せる足場を作る。
  - 根拠: [PC9801/doc/introduction/architecture.txt](PC9801/doc/introduction/architecture.txt), [PC9801/doc/note/boot.txt](PC9801/doc/note/boot.txt)
- 到達: 1st/2nd ブートで FD から起動、テキストコンソールと KBD、FDC/WD33C93 SCSI、PIC/DMA を動作させ、対話的 `init` で検証。
  - 根拠: [PC9801/src/boot/1st/1stboot.s](PC9801/src/boot/1st/1stboot.s), [PC9801/src/boot/2nd](PC9801/src/boot/2nd), [PC9801/src/kernel](PC9801/src/kernel)

## つなぐ意思（コミュニティへの呼びかけ）
- ステータスレポートには、明確な呼びかけと窓口が残っています。これは「一人の実装」から「みんなの OS」へ舵を切るメッセージでした。
  - メーリングリスト/購読方法、WWW、Nifty FTRON での案内:
    - [kernel-status.text](PC9801/doc/note/kernel-status.text#L9-L23)（連絡先: night@bfree.rim.or.jp）
    - [kernel-status.text](PC9801/doc/note/kernel-status.text#L25-L67)（購読/WWW/Nifty 具体手順）
  - 「B-Free はベストな OS ではなく、より良くしていく OS」という宣言と、公開/ライセンス方針の示唆。
    - [kernel-status.text](PC9801/doc/note/kernel-status.text#L69-L134)

## 技術の橋（PC-98 ITRON → BTRON/POSIX）
- 設計の核はメッセージ駆動のマイクロカーネル（ITRON 3.0）と、名前解決のポートマネージャ。ここに BTRON/POSIX を載せる構図が早期から明示。
  - 根拠: [PC9801/doc/note/port-manager.text](PC9801/doc/note/port-manager.text), [PC9801/doc/note/posix.text](PC9801/doc/note/posix.text)
- PC-98 ポートで得た「動く最小核（割込み/IPC/メモリ/デバイス）」が、そのまま上位マネージャ群（PM/FM/MM 等）を受け止める土台になりました。

## btron-pc へ（BTRON マネージャの結晶）
- `btron-pc/` は、BTRON のマネージャ群（MM/PM/FM 他）と POSIX シェル/マネージャの側を体系化した系統です。
  - 例: [btron-pc/kernel/BTRON/manager/MM](btron-pc/kernel/BTRON/manager/MM), [btron-pc/kernel/BTRON/manager/PM](btron-pc/kernel/BTRON/manager/PM)
  - POSIX シェル系: [btron-pc/kernel/POSIX/shell](btron-pc/kernel/POSIX/shell)
- PC-98 の ITRON 基盤とサーバ/ドライバ群は、「どこに何を載せるか」のインターフェイス（ポート/メッセージ）を固め、btron-pc で BTRON の形が磨かれていく足場となりました。
- 貢献者の面でも、PC-98 側の初期コミッタと btron-pc の著者群が連続して現れます。
  - 著者一覧: [btron-pc/AUTHORS](btron-pc/AUTHORS)（SCSI/ドライバ、スクリプト言語、他）
  - PC-98 側の痕跡: [PC9801/CONTRIBUTORS.md](PC9801/CONTRIBUTORS.md)

## 残されたメッセージ（意図と言葉）
- 「協力者の募集」「公開の意義」「継続的に良くしていく」という価値観が、当時のノートに明確に刻まれています。
  - 呼びかけの文面や連絡先は、後年の CVS 取込み後もそのまま残り、プロジェクトの原風景を伝えています。
    - 例: [kernel-status.text](PC9801/doc/note/kernel-status.text#L1-L23), [kernel-status.text](PC9801/doc/note/kernel-status.text#L134-L140)

## 歴史的な意味
- 技術面: 国産 PC-98 ハード上で TRON 系 OS を「自力で起動・入出力・記憶装置まで」到達させたこと自体が大きな実績。これが上位 API（BTRON/POSIX）の実装/検証環境を現実世界に引き寄せました。
- コミュニティ面: オープンな呼びかけとメールing list/ネット掲示の活用は、当時として先進的でした。成果物/連絡先/参加方法を日本語で明示したことは、国内開発者の参入障壁を下げました。
- 系譜面: PC-98 ポートで固めた OS 中核とツール群が、`btron-pc/` 側の BTRON マネージャと合流し、後続の B-Free/BTRON 系の動く実装の「背骨」になりました。

---

更新の提案
- 口述史アネックス: 初期メンバーの回想/コメントがあれば、本ファイル末尾に追補（年代順に引用）。
- btron-pc 側年表: `btron-pc/` の初出/主要変更を抽出し、[PC9801/HISTORY.md](PC9801/HISTORY.md) のタイムラインと対比掲載。

---

## GitHub に残るまで（RCS/CVS の痕跡から）

このコード群は、個人環境の RCS/CVS 管理からはじまり、2011 年の CVS 由来インポートを経て、現行の Git リポジトリ（GitHub に保全される形）へと受け継がれました。

- ローカル RCS/CVS 期の痕跡:
  - [PC9801/src/kernel/itron-3.0/i386/virtual_memory.c](PC9801/src/kernel/itron-3.0/i386/virtual_memory.c#L43-L45) に「/home/night/CVS/...」「1996-01-06 night Exp」という `$Header` が残存。個人 CVS による継続開発の存在を示します。
- 1995 年の bring-up 期（FD ブート/デバイスの初搭載）:
  - [PC9801/ChangeLog](PC9801/ChangeLog) の 1995-09/10 エントリ（FD ドライバ、init 追加）。
- 2011-12-27 の CVS 由来インポート（履歴の起点化）:
  - 多数ファイルに「v 1.1 2011/12/27 ... liu1 Exp」「Initial Version.」のヘッダが付与。
    - 例: [PC9801/src/posix/usr/src/sys/lowlib/mm.c](PC9801/src/posix/usr/src/sys/lowlib/mm.c#L11-L12)
    - 例: [PC9801/src/posix/usr/src/sys/server/PM/proctable.c](PC9801/src/posix/usr/src/sys/server/PM/proctable.c#L11-L12)
- Git への継承と GitHub 保全:
  - 2011 年インポート時点のスナップショットが、以後の Git リポジトリに取り込まれ、現行の GitHub 上の保存物の「実質的な起点」として残っています（個別コミットの細粒度は CVS の外側にあり、ファイル先頭の `$Header` が当時の出自を伝えます）。

### 残されたメッセージ（当時からの手紙）

- 連絡先と協力の呼びかけ、公開の意義、そして「B-Free はベスト（best）ではなく、より良く（better）していく OS」という価値観が、いまも一次資料に読めます。
  - 連絡先/購読/WWW/Nifty: [kernel-status.text](PC9801/doc/note/kernel-status.text#L9-L67)
  - 開発思想・公開方針の記述: [kernel-status.text](PC9801/doc/note/kernel-status.text#L69-L134)

この「呼びかけ」は、CVS→Git への媒体移行後もファイルに刻まれ続け、今日 GitHub に残るソースの文脈（背景と志）を静かに伝えています。

---

## 対比タイムライン（PC9801 × btron-pc）

- 1991-06（btron-pc 系譜の源流）
  - BTRON Manager/POSIX Shell に「Version 2, June 1991」の表記
    - 例: [btron-pc/kernel/POSIX/shell/shell.c](btron-pc/kernel/POSIX/shell/shell.c#L6), [btron-pc/kernel/POSIX/shell/buildins.c](btron-pc/kernel/POSIX/shell/buildins.c#L6)

- 1994（PC-98 向け起動設計の言葉）
  - 2段ブート、メモリマップ、コマンド仕様の設計メモ
    - [PC9801/doc/note/boot.txt](PC9801/doc/note/boot.txt)

- 1995（PC-98 実機での bring-up）
  - FD ドライバ追加、`init` 追加などの到達点を記録
    - [PC9801/ChangeLog](PC9801/ChangeLog)

- 1996-01（ITRON/i386 仮想記憶の更新痕跡）
  - 個人 CVS の `$Header`（night）に 1996-01-06 の記録
    - [PC9801/src/kernel/itron-3.0/i386/virtual_memory.c](PC9801/src/kernel/itron-3.0/i386/virtual_memory.c#L43-L45)

- 2011-12-27（CVS 由来のスナップショット取り込み）
  - 多数ファイルに「v 1.1 2011/12/27 ... liu1 Exp」「Initial Version.」
    - 例: [PC9801/src/posix/usr/src/sys/lowlib/mm.c](PC9801/src/posix/usr/src/sys/lowlib/mm.c#L11-L12), [PC9801/src/posix/usr/src/sys/server/PM/proctable.c](PC9801/src/posix/usr/src/sys/server/PM/proctable.c#L11-L12)

- （以後）Git に継承され、GitHub 上で保全
  - CVS 時点のスナップショットが、現行リポジトリの実質的起点として残存

## 自動抽出年表（btron-pc）

- 生成物: [PC9801/tools/btronpc_timeline.md](PC9801/tools/btronpc_timeline.md)
- 生成スクリプト: [PC9801/tools/extract_btronpc_timeline.ps1](PC9801/tools/extract_btronpc_timeline.ps1)
- 備考: ヘッダや `$Header` の年を走査して簡易年表を作成（最多3件/年で抜粋）。

---

## 実行手順（32ビット仮想起動）

前提
- QEMU（`qemu-system-i386`）がインストール済み。
- 作業ディレクトリ: `btron-pc/boot/2nd`。

手順
1) フロッピーイメージの生成（32bit）
  - スクリプト: [btron-pc/boot/2nd/create_bootable_floppy_32.ps1](btron-pc/boot/2nd/create_bootable_floppy_32.ps1)
```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
& "c:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd\create_bootable_floppy_32.ps1"
```
  - 生成物: `bootable.img`

2) QEMUで起動（i386）
  - スクリプト: [btron-pc/boot/2nd/run_qemu_32_floppy.ps1](btron-pc/boot/2nd/run_qemu_32_floppy.ps1)
```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
& "c:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd\run_qemu_32_floppy.ps1"
```
  - 直接起動の例（参考）:
```powershell
Push-Location "c:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd"
qemu-system-i386 -m 64 -fda bootable.img -boot a -vga std -serial mon:stdio -drive format=raw,if=floppy,file=bootable.img
Pop-Location
```

オプション
- メモリ変更: `run_qemu_32_floppy.ps1 -Mem 128` など。
- 画像名変更: `create_bootable_floppy_32.ps1 -OutputImage myboot.img`。

注意
- ISO系スクリプト（`make_iso.ps1`/`create_bootable_iso.ps1`）は `2ndboot64` を前提としており、32bit用途では上記フロッピー手順が簡便です。
- 既定のブートイメージ名は `2ndboot`。存在しない場合は `create_bootable_floppy_32.ps1` がエラーを出します。

### スクリーンショット（起動確認の証跡）

- 取得例（診断イメージ）: [btron-pc/boot/2nd/screenshots/boot-20251219-035444.png](btron-pc/boot/2nd/screenshots/boot-20251219-035444.png)
- 取得例（2ndブート）: [btron-pc/boot/2nd/screenshots/boot-20251219-035716-2nd.png](btron-pc/boot/2nd/screenshots/boot-20251219-035716-2nd.png)
- 取得手順（自動）:
  - [btron-pc/boot/2nd/capture_boot_screenshot.ps1](btron-pc/boot/2nd/capture_boot_screenshot.ps1) を使用
  - または、QEMUを `-monitor tcp:...` で起動後に `screendump screenshots/boot-xxxx.ppm` を送信
  - PPM → PNG 変換: [btron-pc/boot/2nd/ppm_to_png.ps1](btron-pc/boot/2nd/ppm_to_png.ps1)
