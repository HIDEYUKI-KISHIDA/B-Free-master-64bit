param(
    [string]$Image = "bootable.img",
    [int]$Mem = 64
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Image)) { Write-Error "Image not found: $Image"; exit 1 }
if (-not (Get-Command "qemu-system-i386" -ErrorAction SilentlyContinue)) {
    Write-Error "qemu-system-i386 not found. Please check PATH."; exit 1
}

$args = @(
    "-m", $Mem,
    "-boot", "a",
    "-vga", "std",
    "-serial", "mon:stdio",
    "-drive", "format=raw,if=floppy,file=$Image"
)

Write-Host ("Launching: qemu-system-i386 " + ($args -join ' ')) -ForegroundColor Cyan
qemu-system-i386 @args
