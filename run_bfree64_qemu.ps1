# QEMUでB-Free 64ビット版を簡単に起動するスクリプト
# Windows用（PowerShell）

# ビルド済みのイメージファイル名（例: bfree-64bit.iso）
$iso = "bfree-64bit.iso"

# QEMUのパス（必要に応じて修正）
$qemu = "qemu-system-x86_64"

# メモリサイズ
$mem = 512

# QEMU起動コマンド
Write-Host "QEMUでB-Free 64bitを起動します..."
& $qemu -m $mem -cdrom $iso
