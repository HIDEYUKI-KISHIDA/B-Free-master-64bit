param(
    [Parameter(Mandatory=$true)]
    [string]$InputPpm,
    [string]$OutputPng
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $InputPpm)) {
    Write-Error "PPM not found: $InputPpm"
    exit 1
}

if (-not $OutputPng) {
    $OutputPng = [System.IO.Path]::ChangeExtension($InputPpm, ".png")
}

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Runtime.InteropServices

function Read-Token([System.IO.BinaryReader]$br) {
    # Skip whitespace
    while ($true) {
        $b = $br.PeekChar()
        if ($b -eq -1) { throw "Unexpected EOF while skipping whitespace" }
        $c = [char]$b
        if ($c -eq '#') {
            # skip comment to end of line
            while ($true) {
                $bb = $br.Read()
                if ($bb -eq -1) { throw "Unexpected EOF in comment" }
                if ([char]$bb -eq "`n") { break }
            }
            continue
        }
        if (" `t`r`n".IndexOf($c) -ge 0) {
            $null = $br.Read() # consume
            continue
        }
        break
    }
    # Read token
    $sb = New-Object System.Text.StringBuilder
    while ($true) {
        $b = $br.PeekChar()
        if ($b -eq -1) { break }
        $c = [char]$b
        if (" `t`r`n".IndexOf($c) -ge 0 -or $c -eq '#') { break }
        $null = $sb.Append([char]$br.Read())
    }
    if ($sb.Length -eq 0) { throw "Failed to read token" }
    return $sb.ToString()
}

$fs = [System.IO.File]::Open($InputPpm, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
try {
    $br = New-Object System.IO.BinaryReader($fs, [System.Text.Encoding]::ASCII, $true)
    $magic = Read-Token $br
    if ($magic -ne 'P6') { throw "Unsupported PPM format: $magic (expected P6)" }
    $w = [int](Read-Token $br)
    $h = [int](Read-Token $br)
    $maxv = [int](Read-Token $br)
    if ($maxv -ne 255) { throw "Unsupported maxval: $maxv (expected 255)" }
    # Consume one single whitespace after header if present
    $next = $br.Read()
    if ($next -eq -1) { throw "Unexpected EOF before pixel data" }

    $expected = [int64]$w * [int64]$h * 3
    $data = $br.ReadBytes($expected)
    if ($data.Length -ne $expected) { throw "Pixel data truncated: got $($data.Length), expected $expected" }

    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $rect = New-Object System.Drawing.Rectangle(0,0,$w,$h)
    $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
    try {
        $stride = [Math]::Abs($bd.Stride)
        $rowBytes = $w * 3
        $dst = New-Object byte[] ($stride * $h)
        $src = $data
        for ($y=0; $y -lt $h; $y++) {
            $srcOff = $y * $rowBytes
            $dstOff = $y * $stride
            for ($x=0; $x -lt $w; $x++) {
                $si = $srcOff + $x*3
                $di = $dstOff + $x*3
                # PPM: R,G,B -> Bitmap: B,G,R
                $dst[$di+0] = $src[$si+2]
                $dst[$di+1] = $src[$si+1]
                $dst[$di+2] = $src[$si+0]
            }
        }
        [System.Runtime.InteropServices.Marshal]::Copy($dst, 0, $bd.Scan0, $dst.Length)
    } finally {
        $bmp.UnlockBits($bd)
    }
    $bmp.Save($OutputPng, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
} finally {
    $fs.Dispose()
}

Write-Host "Saved PNG: $OutputPng" -ForegroundColor Green
