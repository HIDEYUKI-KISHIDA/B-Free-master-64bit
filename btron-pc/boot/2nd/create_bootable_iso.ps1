# Create a bootable ISO with proper MBR and boot code

param(
    [string]$BootImagePath = "2ndboot64",
    [string]$OutputISO = "bfree-64bit-bootable.iso"
)

Write-Host "Creating Bootable ISO with MBR..." -ForegroundColor Cyan

# Check if boot image exists
if (-not (Test-Path $BootImagePath)) {
    Write-Error "Boot image '$BootImagePath' not found!"
    exit 1
}

# Read boot image
$bootData = [System.IO.File]::ReadAllBytes($BootImagePath)
$bootSize = $bootData.Length

Write-Host "Boot image size: $bootSize bytes" -ForegroundColor Green

# Create ISO with proper El Torito boot structure
$isoData = New-Object System.Collections.ArrayList

# 1. System Area (16 sectors = 32KB of zeros)
for ($i = 0; $i -lt 16; $i++) {
    $isoData.AddRange([byte[]]@(0) * 2048) | Out-Null
}

# 2. Boot Catalog (at sector 17, LBA 0x11)
$bootCatalog = New-Object byte[] 2048
$bootCatalog[0] = 0x01    # Validation Entry
$bootCatalog[1] = 0x00    # Platform ID (0x00 = 80x86)
$bootCatalog[2] = 0x00
$bootCatalog[3] = 0x00
$bootCatalog[4] = 0xAA    # Checksum (placeholder)
$bootCatalog[5] = 0x55    # Signature byte 1
$bootCatalog[6] = 0xEB    # Signature byte 2 (255)

# Initial/Default Entry at offset 32
$bootCatalog[32] = 0x88   # Boot Indicator (bootable)
$bootCatalog[33] = 0x00   # Platform ID (0x00 = 80x86)
$bootCatalog[34] = 0x01   # Sector count (1 sector)
$bootCatalog[35] = 0x00
$bootCatalog[36] = 0x12   # Boot LBA (0x12 = 18 in little endian)
$bootCatalog[37] = 0x00
$bootCatalog[38] = 0x00
$bootCatalog[39] = 0x00

$isoData.AddRange($bootCatalog) | Out-Null

# 3. Boot Image (at sector 18, LBA 0x12)
$bootImage = New-Object byte[] 2048
[System.Buffer]::BlockCopy($bootData, 0, $bootImage, 0, [Math]::Min($bootSize, 2048))
$isoData.AddRange($bootImage) | Out-Null

# 4. Primary Volume Descriptor (at sector 19, LBA 0x13)
$pvd = New-Object byte[] 2048
$pvd[0] = 0x01    # Type code for PVD
[System.Text.Encoding]::ASCII.GetBytes("CD001", 0, 5) | % {
    for ($i = 0; $i -lt 5; $i++) {
        $pvd[$i + 1] = $_[$i]
    }
}
$pvd[6] = 0x01    # Version

# Space size (total sectors) - little endian and big endian
$totalSectors = [Math]::Ceiling($isoData.Count / 2048) + 1
$totalSectorsLE = [BitConverter]::GetBytes([int32]$totalSectors)
$totalSectorsBE = [byte[]]@($totalSectorsLE[3], $totalSectorsLE[2], $totalSectorsLE[1], $totalSectorsLE[0])

$pvd[80]  = $totalSectorsLE[0]
$pvd[81]  = $totalSectorsLE[1]
$pvd[82]  = $totalSectorsLE[2]
$pvd[83]  = $totalSectorsLE[3]
$pvd[84]  = $totalSectorsBE[0]
$pvd[85]  = $totalSectorsBE[1]
$pvd[86]  = $totalSectorsBE[2]
$pvd[87]  = $totalSectorsBE[3]

# Volume set size
$pvd[120] = 0x01
$pvd[121] = 0x00
$pvd[122] = 0x00
$pvd[123] = 0x01
$pvd[124] = 0x01
$pvd[125] = 0x00
$pvd[126] = 0x00
$pvd[127] = 0x01

# Logical block size (2048) - little endian and big endian
$pvd[128] = 0x00
$pvd[129] = 0x08
$pvd[130] = 0x00
$pvd[131] = 0x00
$pvd[132] = 0x00
$pvd[133] = 0x00
$pvd[134] = 0x08
$pvd[135] = 0x00

# Path table size
$pvd[136] = 0x0A
$pvd[137] = 0x00
$pvd[138] = 0x00
$pvd[139] = 0x00
$pvd[140] = 0x00
$pvd[141] = 0x00
$pvd[142] = 0x00
$pvd[143] = 0x0A

# Publication identifier
$pubId = "B-Free OS 64-bit"
$pubIdBytes = [System.Text.Encoding]::ASCII.GetBytes($pubId)
[System.Buffer]::BlockCopy($pubIdBytes, 0, $pvd, 318, [Math]::Min($pubIdBytes.Length, 128))

$isoData.AddRange($pvd) | Out-Null

# 5. Volume Descriptor Set Terminator (at sector 20, LBA 0x14)
$terminator = New-Object byte[] 2048
$terminator[0] = 0xFF    # Type code for terminator
[System.Text.Encoding]::ASCII.GetBytes("CD001", 0, 5) | % {
    for ($i = 0; $i -lt 5; $i++) {
        $terminator[$i + 1] = $_[$i]
    }
}
$terminator[6] = 0x01

$isoData.AddRange($terminator) | Out-Null

# Pad to complete sectors
$remainder = $isoData.Count % 2048
if ($remainder -ne 0) {
    $padding = 2048 - $remainder
    $isoData.AddRange([byte[]]@(0) * $padding) | Out-Null
}

# Write ISO file
try {
    [System.IO.File]::WriteAllBytes($OutputISO, $isoData.ToArray())
    Write-Host "Bootable ISO created: $OutputISO" -ForegroundColor Green
    Write-Host "ISO size: $([System.IO.File]::ReadAllBytes($OutputISO).Length) bytes" -ForegroundColor Green
    Write-Host ""
    Write-Host "To run with QEMU:" -ForegroundColor Yellow
    Write-Host "  qemu-system-x86_64 -m 512 -cdrom $OutputISO"
    Write-Host ""
}
catch {
    Write-Error "Failed to create ISO: $_"
    exit 1
}
