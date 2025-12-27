# Create a simple bootable ISO for B-Free OS 64-bit
# This script creates a minimal ISO without requiring external tools

param(
    [string]$BootImagePath = "2ndboot64",
    [string]$OutputISO = "bfree-64bit.iso"
)

Write-Host "B-Free OS 64-bit ISO Generator" -ForegroundColor Cyan
Write-Host "=================================" -ForegroundColor Cyan

# Check if boot image exists
if (-not (Test-Path $BootImagePath)) {
    Write-Error "Boot image '$BootImagePath' not found!"
    Write-Host ""
    Write-Host "Please build the bootloader first:"
    Write-Host "  make clean"
    Write-Host "  make 2ndboot64"
    exit 1
}

Write-Host "Boot image: $BootImagePath" -ForegroundColor Green
$bootSize = (Get-Item $BootImagePath).Length
Write-Host "Boot image size: $bootSize bytes" -ForegroundColor Green

# Read boot image
$bootData = [System.IO.File]::ReadAllBytes($BootImagePath)

# Create ISO structure
$isoData = New-Object System.Collections.ArrayList

# System area (16 sectors of 2048 bytes = 32KB of zeros)
for ($i = 0; $i -lt 16; $i++) {
    $isoData.AddRange([byte[]]@(0) * 2048) | Out-Null
}

# Primary Volume Descriptor
$pvd = New-Object byte[] 2048
$pvd[0] = 1  # Type code for PVD
[System.Text.Encoding]::ASCII.GetBytes("CD001", 0, 5) | % { 
    for ($i = 0; $i -lt 5; $i++) { 
        $pvd[$i + 1] = $_[$i] 
    } 
}
$pvd[6] = 1  # Version

# Volume set size (1)
$pvd[80] = 1
$pvd[81] = 0
$pvd[82] = 0
$pvd[83] = 1

# Volume sequence number (1)
$pvd[84] = 1
$pvd[85] = 0
$pvd[86] = 0
$pvd[87] = 1

# Logical block size (2048)
$pvd[128] = 0
$pvd[129] = 8
$pvd[130] = 0
$pvd[131] = 0

$isoData.AddRange($pvd) | Out-Null

# Volume Descriptor Set Terminator
$terminator = New-Object byte[] 2048
$terminator[0] = 255  # Type code for terminator
[System.Text.Encoding]::ASCII.GetBytes("CD001", 0, 5) | % { 
    for ($i = 0; $i -lt 5; $i++) { 
        $terminator[$i + 1] = $_[$i] 
    } 
}
$terminator[6] = 1

$isoData.AddRange($terminator) | Out-Null

# Add boot image
$isoData.AddRange($bootData) | Out-Null

# Pad to sector boundary
$remainder = $isoData.Count % 2048
if ($remainder -ne 0) {
    $padding = 2048 - $remainder
    $isoData.AddRange([byte[]]@(0) * $padding) | Out-Null
}

# Write ISO file
try {
    [System.IO.File]::WriteAllBytes($OutputISO, $isoData.ToArray())
    Write-Host "ISO created successfully: $OutputISO" -ForegroundColor Green
    Write-Host "ISO size: $([System.IO.File]::ReadAllBytes($OutputISO).Length) bytes" -ForegroundColor Green
    Write-Host ""
    Write-Host "To run with QEMU:" -ForegroundColor Yellow
    Write-Host "  qemu-system-x86_64 -cdrom $OutputISO"
    Write-Host ""
    Write-Host "To run with VirtualBox:" -ForegroundColor Yellow
    Write-Host "  1. Create a new VM"
    Write-Host "  2. Set CD/DVD to $OutputISO"
    Write-Host "  3. Boot from CD/DVD"
}
catch {
    Write-Error "Failed to create ISO: $_"
    exit 1
}
