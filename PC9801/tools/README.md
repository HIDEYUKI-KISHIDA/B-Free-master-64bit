# PC-98 検証ツール（Windows）

このフォルダには、PC-9801 用フロッピー（1.23MB）イメージ生成と DOSBox‑X(pc98) 起動を自動化する PowerShell スクリプトを収めています。実機がなくても 2nd ブートの `boot>` 到達までの検証が可能です。

## フロッピー仕様（PC-98）
- 1.23MB（77T × 2H × 8S × 1024B = 1,261,568 bytes）
- 一般的な PC/AT の 1.44MB/1.2MB とは規格が異なるため、必ず pc98 モードで起動してください。

## スクリプト一覧
- `create_pc98_floppy_images.ps1`: `bootimage` と `itron.image` から 1.23MB raw イメージを生成。`-Run` 指定で DOSBox‑X(pc98) を起動。
- `run_pc98_dosboxx.ps1`: 生成済みイメージを指定して DOSBox‑X(pc98) を起動（A=boot、B=itron）。

## 前提
- まず PC-98 側のビルドを完了させてください：
  - `PC9801/src/boot/` で `bootimage` を生成
  - `PC9801/src/kernel/itron-3.0/make/` で `itron.image` を生成
- DOSBox‑X をインストール（Windows 版）。`dosbox-x` が PATH にない場合はスクリプト引数 `-DosboxXExe` で指定できます。

## 使い方（PowerShell）
```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

# 1) 1.23MB イメージ生成（A/B）。必要なら先に PC9801 側をビルドしておく。
& .\PC9801\tools\create_pc98_floppy_images.ps1

# 2) 起動確認（pc98）。DOSBox‑X のパスが通っていない場合は引数で指定。
& .\PC9801\tools\run_pc98_dosboxx.ps1
# 例：
# & .\PC9801\tools\run_pc98_dosboxx.ps1 -DosboxXExe "C:\Apps\dosbox-x\dosbox-x.exe" -MemMB 8
```

## ヒント
- 画面が出ない/ブートしない場合：
  - `bootimage`/`itron.image` の生成を再確認（WSL/MSYS2 で `make`）。
  - `dosbox-x -set machine=pc98` が効いているか確認。
- 32ビット版の検証を急ぐ場合：
  - `btron-pc/boot/2nd` の `create_bootable_floppy_32.ps1` と `run_qemu_32_floppy.ps1` による i386/QEMU 起動が最短です。

## 参考
- PC-98 ブート/設計: `PC9801/doc/note/boot.txt`
- 全体史/位置づけ: `PC9801/NARRATIVE.md`, `PC9801/HISTORY.md`

## 自動化作業完了後のワンクリック実行

[![PC-98 OS自動ビルド＆起動](https://img.shields.io/badge/Run-PC98%20Auto%20Boot-blue?style=for-the-badge)](command:workbench.action.tasks.runTask?task=PC-98%20OS自動ビルド＆起動)