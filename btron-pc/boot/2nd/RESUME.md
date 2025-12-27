# 2nd Boot 可視化・再現メモ（作業再開用）

このメモは「起動時に文字が見えない」問題の調査・対処内容と、再現手順／次アクションを簡潔にまとめたものです。

## 直近の変更点（反映済みファイル）
- create_bootable_floppy_32.ps1: 実モードローダに BIOS テレタイプで「Loading 2nd...」出力を追加。Diagnostic イメージ生成機能あり。
- lib.c: `write_vram()` が属性バイト（上位）を必ず書くよう修正。
- main.c: `_main()` 冒頭で `vga_text()` を呼び出し、コンソール初期化前にテキストモードを強制。
- capture_boot_screenshot.ps1: HMP/QMP 両対応。待機時間（`-WaitSec`）や制御種別（`-Control`）を切替可能。

## すぐに画面を確認する（再現手順）
### 診断イメージ（必ず文字が出る）
1) フロッピー生成
```powershell
Push-Location "c:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd"
./create_bootable_floppy_32.ps1 -Diagnostic
```
2) 手動起動（目視確認）
```powershell
qemu-system-i386 -m 64 -fda .\bootable_diag.img -boot a -vga std
Pop-Location
```

### 2nd イメージ（開発者ペイロード）
1) フロッピー生成（ローダが「Loading 2nd...」を表示）
```powershell
Push-Location "c:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd"
./create_bootable_floppy_32.ps1
```
2) 手動起動（目視確認）
```powershell
qemu-system-i386 -m 64 -fda .\bootable.img -boot a -vga std
Pop-Location
```

### スクリーンショット自動取得（必要時）
- QMP で安定しない場合は HMP/TCP に切替＆待機を長めに
```powershell
Push-Location "c:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd"
./capture_boot_screenshot.ps1 -Image "./bootable.img" -OutputDir "./screenshots" -Control hmp -MonitorMode tcp -WaitSec 12
Pop-Location
```
- 診断イメージの撮影
```powershell
Push-Location "c:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd"
./capture_boot_screenshot.ps1 -Image "./bootable_diag.img" -OutputDir "./screenshots" -Control hmp -MonitorMode tcp -WaitSec 8
Pop-Location
```

## 2nd 本体の恒久対策を反映するには（ビルド）
- 目的: `lib.c` と `main.c` の修正を 2nd 本体に反映（常に文字が見える）
- オプションA: MSYS2 MinGW32（PE32）
  - 前提: `gcc`, `ld`, `make` が利用可能（MSYS2 MinGW32 シェルで実行）。
  - ビルド:
    ```sh
    make USE_MINGW32=1 2ndboot
    ```
- オプションB: i686-elf クロス（ELF32）
  - 前提: `i686-elf-gcc`, `i686-elf-ld` が PATH にあること。
  - ビルド:
    ```sh
    make ELF_TOOLCHAIN=1 2ndboot
    ```
- 生成後: 通常どおり `./create_bootable_floppy_32.ps1` → 起動／撮影

## 既存の証跡（参照画像）
- 診断ショット: btron-pc/boot/2nd/screenshots/boot-20251219-035444.png
- 2nd ショット: btron-pc/boot/2nd/screenshots/boot-20251219-035716-2nd.png

## 既知の注意点
- QEMU 制御ポートの準備が遅れることあり（"Control port not ready"）。その場合は `-WaitSec` を増やすか、HMP/TCP を使用。
- Windows 環境では screendump パスに相対/スラッシュを使うと安定（スクリプトは対応済み）。

## 次のアクション（推奨）
1) ビルド環境の用意（MSYS2 MinGW32 or i686-elf）
2) `make ... 2ndboot` で 2nd を再ビルド
3) `./create_bootable_floppy_32.ps1` でフロッピー再生成
4) `./capture_boot_screenshot.ps1`（HMP/TCP, `-WaitSec 12`）で撮影→ドキュメントへ反映
