param(
    [string]$BootImagePath = "2ndboot",
    [string]$OutputImage = "bootable.img",
    [switch]$Diagnostic,
    [string]$DiagnosticImage = "bootable_diag.img"
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $PSCommandPath
Set-Location $ScriptDir

if ($Diagnostic) {
    # Create a tiny diagnostic boot image that prints a message, then halts
    $bootCode = @()
    $append = { param([byte[]]$arr) $script:bootCode += $arr }
    # Padding to align like a standard BPB area (not used)
    $pad = New-Object 'System.Byte[]' 62
    & $append ([byte[]](0xEB,0x3C) + $pad)
    # Set segments
    & $append ([byte[]](0xB8,0x00,0x00))      # mov ax,0
    & $append ([byte[]](0x8E,0xD8))           # mov ds,ax
    & $append ([byte[]](0x8E,0xC0))           # mov es,ax
    & $append ([byte[]](0x8E,0xD0))           # mov ss,ax
    & $append ([byte[]](0xBC,0x00,0x7C))      # mov sp,0x7C00
    # video mode 3
    & $append ([byte[]](0xB4,0x00,0xB0,0x03,0xCD,0x10))
    # prepare teletype
    & $append ([byte[]](0xFC))                # cld
    & $append ([byte[]](0xB4,0x0E))           # mov ah,0x0E
    & $append ([byte[]](0xBB,0x07,0x00))      # mov bx,0x0007
    # mov si, msg_ptr placeholder; we'll patch after laying out code before the message
    $siIndex = $bootCode.Count
    & $append ([byte[]](0xBE,0x00,0x00))
    $msg = [Text.Encoding]::ASCII.GetBytes("B-Free 32-bit BOOT (diagnostic)\r\n`0")
    # print loop: lodsb; cmp al,0; je end; int 10; jmp loop
    & $append ([byte[]](0xAC,0x3C,0x00,0x74,0x06,0xCD,0x10,0xEB,0xF6))
    # end: hlt; jmp $
    & $append ([byte[]](0xF4,0xEB,0xFE))
    # append message (after code) and patch SI to its absolute address (0x7C00+offset)
    $msgOffsetAbs = 0x7C00 + $bootCode.Count
    $bootCode[$siIndex+1] = [byte]($msgOffsetAbs -band 0xFF)
    $bootCode[$siIndex+2] = [byte](($msgOffsetAbs -shr 8) -band 0xFF)
    # append message
    & $append ($msg)
    while ($bootCode.Count -lt 510) { $bootCode += [byte]0x00 }
    $bootCode += [byte]0x55; $bootCode += [byte]0xAA
    $outPath = Join-Path $ScriptDir $DiagnosticImage
    [IO.File]::WriteAllBytes($outPath, $bootCode)
    Write-Host "Diagnostic floppy image created: $DiagnosticImage" -ForegroundColor Green
    Write-Host "To run: qemu-system-i386 -m 64 -fda $DiagnosticImage -boot a -vga std"
    exit 0
}

if (-not (Test-Path $BootImagePath)) {
    Write-Error "Boot image not found: $BootImagePath. Build it first (make 2ndboot)."
    exit 1
}

# Minimal real-mode boot sector with CHS loop: read N sectors to 0x0000:0x8000, then far jump.
$bootCode = @(
    0xEB,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    # segments
    0xB8,0x00,0x00,      # mov ax,0
    0x8E,0xD8,            # mov ds,ax
    0x8E,0xC0,            # mov es,ax
    0x8E,0xD0,            # mov ss,ax
    0xBC,0x00,0x7C,       # mov sp,0x7C00
    # video mode 3
    0xB4,0x00,0xB0,0x03,0xCD,0x10,
    # clear screen
    0xB4,0x06,0xB0,0x00,0xB7,0x0F,0xB9,0x00,0x00,0xBA,0x4F,0x18,0xCD,0x10,
    # cursor top-left
    0xB4,0x02,0xB7,0x00,0xBA,0x00,0x00,0xCD,0x10,
    # print a short banner via BIOS teletype
    0xB4,0x0E,            # mov ah,0x0E
    0xBB,0x07,0x00,       # mov bx,0x0007
    0xB0,0x4C,0xCD,0x10,  # 'L'
    0xB0,0x6F,0xCD,0x10,  # 'o'
    0xB0,0x61,0xCD,0x10,  # 'a'
    0xB0,0x64,0xCD,0x10,  # 'd'
    0xB0,0x69,0xCD,0x10,  # 'i'
    0xB0,0x6E,0xCD,0x10,  # 'n'
    0xB0,0x67,0xCD,0x10,  # 'g'
    0xB0,0x20,0xCD,0x10,  # ' '
    0xB0,0x32,0xCD,0x10,  # '2'
    0xB0,0x6E,0xCD,0x10,  # 'n'
    0xB0,0x64,0xCD,0x10,  # 'd'
    0xB0,0x2E,0xCD,0x10,  # '.'
    0xB0,0x2E,0xCD,0x10,  # '.'
    0xB0,0x2E,0xCD,0x10,  # '.'
    0xB0,0x0D,0xCD,0x10,  # '\r'
    0xB0,0x0A,0xCD,0x10,  # '\n'
    # setup read loop for CL=sector(2..18), DH=head(0..1), CH=cyl(0..)
    0xBB,0x00,0x80,       # mov bx,0x8000
    0xB1,0x02,            # mov cl,2  (sector)
    0xB6,0x00,            # mov dh,0  (head)
    0xB5,0x00,            # mov ch,0  (cyl)
    0xBD,0xFF,0x00,       # mov bp,<COUNT> (placeholder: 0x00FF)
    # loop:
    0xB4,0x02,            # mov ah,0x02
    0xB0,0x01,            # mov al,0x01
    0xCD,0x13,            # int 0x13
    0x72,0x1C,            # jc fail (jmp +0x1C)
    0x81,0xC3,0x00,0x02,  # add bx,512
    0x4D,                  # dec bp
    0x74,0x14,            # jz done (jmp +0x14)
    0xFE,0xC1,            # inc cl
    0x80,0xF9,0x13,       # cmp cl,19
    0x72,0xE9,            # jb loop (back -0x17)
    0xB1,0x01,            # mov cl,1
    0xFE,0xC6,            # inc dh
    0x80,0xFE,0x02,       # cmp dh,2
    0x72,0xE0,            # jb loop (back -0x20)
    0xB6,0x00,            # mov dh,0
    0xFE,0xC5,            # inc ch
    0xEB,0xDA,            # jmp loop (back -0x26)
    # done:
    0xEA,0x00,0x80,0x00,0x00,  # jmp 0000:8000
    # fail:
    0xEB,0xFE             # jmp $
) | ForEach-Object { [byte]$_ }

while ($bootCode.Count -lt 510) { $bootCode += [byte]0x00 }
$bootCode += [byte]0x55; $bootCode += [byte]0xAA

$kernelBytes = [IO.File]::ReadAllBytes((Resolve-Path $BootImagePath))
$sectorCount = [int][math]::Ceiling($kernelBytes.Length / 512.0)

# Patch BP (word placeholder 0x00FF) with sector count
$patched = $false
for ($i=0; $i -le $bootCode.Count-3; $i++) {
    if ($bootCode[$i] -eq 0xBD -and $bootCode[$i+1] -eq 0xFF -and $bootCode[$i+2] -eq 0x00) {
        $bootCode[$i+1] = [byte]($sectorCount -band 0xFF)
        $bootCode[$i+2] = [byte](($sectorCount -shr 8) -band 0xFF)
        $patched = $true; break
    }
}
if (-not $patched) { Write-Error "Failed to patch sector count (BP)"; exit 1 }

$totalKernelSize = $sectorCount * 512
if ($kernelBytes.Length -lt $totalKernelSize) {
    $padLen = $totalKernelSize - $kernelBytes.Length
    $pad = New-Object 'System.Byte[]' ($padLen)
    $kernelBytes = $kernelBytes + $pad
}

$outPath = Join-Path $ScriptDir $OutputImage
[IO.File]::WriteAllBytes($outPath, $bootCode + $kernelBytes)
Write-Host "Bootable floppy image created: $OutputImage" -ForegroundColor Green
Write-Host "Sectors: $sectorCount, Size: $($bootCode.Count + $kernelBytes.Length) bytes"
Write-Host "To run: qemu-system-i386 -m 64 -fda $OutputImage -boot a -vga std"
