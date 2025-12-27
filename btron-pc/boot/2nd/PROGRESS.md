# B-Free 2nd boot (32-bit) – Windows/MSYS2 作業ログ

このファイルは、32ビット 2nd boot を Windows/MSYS2 (MinGW32/PE) で構築し、起動可能 ISO を生成するまでの修正点と手順を記録しています。再開時は「再現手順」を実行してください。

## 直近の修正要約
- `start32.S`: IDT 初期化ループ内の誤挿入行を削除し、`rp_sidt` で `movl %eax,(%edi)` を復元。
- `interrupt.S`: `call interrupt` を `call _interrupt` に統一（COFF での名前修飾対策）。
- `endmark.S` 追加: COFF 用のシンボル `_end` を `.bss` に定義し、`OBJ32` の末尾にリンク（`memory.c` の `extern int end;` 解決）。
- `build_boot2.c`: `bzero` を `memset` に置換（MinGW 互換性）。
- `Makefile`:
  - MinGW32/PE 用の `mode32` リンク→`objcopy` によるフラット化ルートを維持。
  - `TOOLS` をツールチェーンに応じて切替（MinGW→`build_boot2`）。
  - `OBJ32` 末尾に `endmark.o` を追加。

### バグ修正（2025-12-18）
- `fd.c`: ステータス判定の演算子優先順位の不具合を修正。
  - 変更前: `if (fd_status.status_data[1] | fd_status.status_data[2] != 0x00)`
  - 変更後: `if ((fd_status.status_data[1] | fd_status.status_data[2]) != 0x00)`
  - 目的: ビットOR結果が0かどうかの判定に括弧を付け、意図通りの比較を保証。

## 再現手順（32-bit, MinGW32）
作業ディレクトリ: `btron-pc/boot/2nd`

1) クリーン＆ビルド
```
make clean
make USE_MINGW32=1 mode32
make USE_MINGW32=1 2ndboot
```

2) ISO 作成（PowerShell 実行ポリシー回避込み）
```
powershell -ExecutionPolicy Bypass -File .\create_bootable_iso.ps1 -BootImagePath 2ndboot -OutputISO bfree-32bit-bootable.iso
```

3) QEMU で起動（QEMU が PATH にある前提）
```
qemu-system-x86_64 -m 512 -cdrom bfree-32bit-bootable.iso
```

- 文字ベースでの確認を優先する場合は `-serial stdio` や `-curses` を併用可能ですが、本 2nd boot は VGA テキスト出力が中心です。

## 既知事項
- 多数の警告は残存しますが、リンク・フラット化・2ndboot 生成は成功しています。
- `start16` のリンク時に `second_boot` の警告が出ますが、出力されたフラットバイナリは `build_boot2` により先頭 32 バイトのヘッダ付きで結合されます。

## 次の一手（起動画面確認）
- QEMU 起動後の表示をキャプチャ。必要に応じて `VGA`/`evaluate` 周りの見た目を微調整します。
- 64-bit 側は未検証。まずは 32-bit のブートシーケンス確認を継続します。

---

## 2025-12-18 進捗スナップショット（再開用メモ）

- スクリーンショット: [btron-pc/boot/2nd/screenshots/boot-latest.png](btron-pc/boot/2nd/screenshots/boot-latest.png)（更新: 21:17）。元PPMは [btron-pc/boot/2nd/screenshots/boot-qmp.ppm](btron-pc/boot/2nd/screenshots/boot-qmp.ppm)。
- 自動化スクリプト: [btron-pc/boot/2nd/auto_capture_boot.ps1](btron-pc/boot/2nd/auto_capture_boot.ps1) と [btron-pc/boot/2nd/capture_boot_screenshot.ps1](btron-pc/boot/2nd/capture_boot_screenshot.ps1)。PPM→PNG 変換は [btron-pc/boot/2nd/ppm_to_png.ps1](btron-pc/boot/2nd/ppm_to_png.ps1)。
- 起動メディア: ISO が不安定な場合はフロッピー/ディスクイメージ（[btron-pc/boot/2nd/bootable.img](btron-pc/boot/2nd/bootable.img), [btron-pc/boot/2nd/bootdisk.img](btron-pc/boot/2nd/bootdisk.img)）で確認。

### 再現クイックスタート（PowerShell）

```powershell
cd C:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd

# 1) ビルド（必要時）
make clean
make USE_MINGW32=1 mode32
make USE_MINGW32=1 2ndboot

# 2) フロッピー/ディスクイメージ作成（ISOが不安定な場合の代替）
powershell -ExecutionPolicy Bypass -File .\create_bootable_floppy_32.ps1 -BootImagePath 2ndboot -OutputImage bootable.img

# 3) 自動起動＆スクショ（QMP推奨）
powershell -ExecutionPolicy Bypass -File .\auto_capture_boot.ps1 -Mem 64 -WaitSec 10 -Control qmp -Display win32

# 4) PPM→PNG 変換（自動で生成されたPPMがある場合）
powershell -ExecutionPolicy Bypass -File .\ppm_to_png.ps1 -InputPpm .\screenshots\boot-qmp.ppm

# 5) 最新画像の確認
Get-Item .\screenshots\boot-latest.png | Select-Object Name,Length,LastWriteTime
```

### 方針メモ（POSIX）
- 目標: ソースレベルのPOSIX互換を優先（libcは `musl`/`newlib` を候補）。
- 非目標: Linuxバイナリ互換（ELF+syscall ABI+/proc の完全互換）は対象外。
- 次アクション（OS側の順序）:
  - POSIXサーバの最小起動テスト（`fork/exec/wait` の骨格確認）
  - VFS/SFS 経由のファイル操作（`open/read/write` 最小）
  - syscall ABI とユーザランドとの結線（libc移植の受け口）

### 未処理の作業（記録）
- ISO/USBブート安定化（El Torito対応の検証・改善）
- PC-9801最小ブートの再現（独自BIOS/I/O仕様に合わせた確認）
- PC-9801エミュレータ選定（Neko Project II / MAME での検証環境整備）

