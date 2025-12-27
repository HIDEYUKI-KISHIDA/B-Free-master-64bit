param(
    [string]$OutputImage = "bootable_diag.img"
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $PSCommandPath
Set-Location $ScriptDir

# Build a simple boot sector that prints a message then halts
$code = New-Object System.Collections.Generic.List[byte]
function AddBytes([byte[]]$b){ $script:code.AddRange($b) }

# 2-byte jmp + 62-byte pad (BPB placeholder area)
AddBytes ([byte[]](0xEB,0x3C));
$pad = New-Object 'System.Byte[]' 62; AddBytes $pad

# Setup segments
AddBytes ([byte[]](0xB8,0x00,0x00))      # mov ax,0
AddBytes ([byte[]](0x8E,0xD8))           # mov ds,ax
AddBytes ([byte[]](0x8E,0xC0))           # mov es,ax
AddBytes ([byte[]](0x8E,0xD0))           # mov ss,ax
AddBytes ([byte[]](0xBC,0x00,0x7C))      # mov sp,0x7C00

# video mode 3
AddBytes ([byte[]](0xB4,0x00,0xB0,0x03,0xCD,0x10))

# teletype setup
AddBytes ([byte[]](0xFC))                # cld
AddBytes ([byte[]](0xB4,0x0E))           # mov ah,0x0E
AddBytes ([byte[]](0xBB,0x07,0x00))      # mov bx,0x0007

# mov si, msg_ptr (placeholder)
$siPos = $code.Count
AddBytes ([byte[]](0xBE,0x00,0x00))

# print loop: lodsb; cmp al,0; je end; int 10; jmp loop
AddBytes ([byte[]](0xAC,0x3C,0x00,0x74,0x06,0xCD,0x10,0xEB,0xF6))

# end: hlt; jmp $
AddBytes ([byte[]](0xF4,0xEB,0xFE))

# message data
$msgBytes = [Text.Encoding]::ASCII.GetBytes("B-Free 32-bit BOOT (diagnostic)\r\n`0")
$msgAbs = 0x7C00 + $code.Count
$code[$siPos+1] = [byte]($msgAbs -band 0xFF)
$code[$siPos+2] = [byte](($msgAbs -shr 8) -band 0xFF)
AddBytes $msgBytes

# pad to 510 and add boot signature
while ($code.Count -lt 510) { $code.Add(0x00) }
$code.Add(0x55); $code.Add(0xAA)

$outPath = Join-Path $ScriptDir $OutputImage
[IO.File]::WriteAllBytes($outPath, $code.ToArray())
Write-Host "Diagnostic floppy image created: $OutputImage" -ForegroundColor Green
Write-Host "To run: qemu-system-i386 -m 64 -fda $OutputImage -boot a -vga std"
