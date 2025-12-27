#!/bin/bash
# PC-9801自動ビルド・イメージ生成スクリプト（WSL用）
set -e

# 必要なパッケージをインストール
sudo apt update
sudo apt install -y bin86 gcc make

# 作業ディレクトリへ移動
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/PC9801/src/boot

# bootimage生成
make clean
make image

# bootimageをWindows側toolsフォルダへコピー
cp bootimage /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/PC9801/tools/

echo "bootimage生成・コピー完了。次はWindowsでPowerShellスクリプトを実行してください。"
