param(
    [string]$ISO = "bfree-32bit-bootable.iso",
    [int]$Mem = 512
)

if (-not (Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue)) {
    Write-Error "qemu-system-x86_64 が見つかりません。PATH を確認してください。"
    exit 1
}

$qemuArgs = @(
    "-m", $Mem,
    "-cdrom", $ISO,
    "-boot", "d"
)

Write-Host "Launching QEMU: qemu-system-x86_64 $($qemuArgs -join ' ')" -ForegroundColor Cyan
qemu-system-x86_64 @qemuArgs
