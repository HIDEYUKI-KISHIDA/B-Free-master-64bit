param(
    [string]$BootImagePath = "c:\Users\h_kis\Desktop\B-Free-master\Program\PC9801\src\boot\bootimage",
    [string]$BootOut = "c:\Users\h_kis\Desktop\B-Free-master\Program\PC9801\tools\pc98_boot.img",
    [string]$ItronImagePath = "c:\Users\h_kis\Desktop\B-Free-master\Program\PC9801\src\kernel\itron-3.0\make\itron.image",
    [string]$ItronOut = "c:\Users\h_kis\Desktop\B-Free-master\Program\PC9801\tools\pc98_itron.img",
    [string]$Format = "pc98_123",
    [switch]$SkipBoot = $false,
    [switch]$SkipItron = $false,
    [switch]$Run = $false,
    [switch]$NoExit = $false
)
Write-Host "[1/3] Parameter check complete" -ForegroundColor Cyan
$ErrorActionPreference = 'Stop'
function Get-TargetSizeBytes {
    param([Parameter(Mandatory)][ValidateSet('pc98_123','ibm_144')] [string]$Format)
    switch ($Format) {
        'pc98_123' { return 1261568 }
        'ibm_144'  { return 1474560 }
    }
}
function New-Pc98ImageFromFile {
    param(
        [Parameter(Mandatory)] [string]$InputFile,
        [Parameter(Mandatory)] [string]$OutputImage,
        [int]$TargetSize = 1261568
    )
    if (-not (Test-Path $InputFile)) {
        throw "Input file not found: $InputFile"
    }
    Write-Host "[2/3] Image creation started" -ForegroundColor Cyan
    Write-Host "  - Reading: $InputFile" -ForegroundColor Gray
    $inBytes = [System.IO.File]::ReadAllBytes($InputFile)
    $len = $inBytes.Length
    Write-Host "  - Bytes read: $len" -ForegroundColor Gray
    $copyLen = [Math]::Min($len, $TargetSize)
    Write-Host "  - Bytes to copy: $copyLen (max $TargetSize)" -ForegroundColor Gray
    $outBytes = New-Object byte[] ($TargetSize)
    $step = 1024 * 128
    $logFile = "c:\Users\h_kis\Desktop\B-Free-master\Program\PC9801\tools\progress.log"
    if (Test-Path $logFile) { Remove-Item $logFile }
    for ($i = 0; $i -lt $copyLen; $i += $step) {
        $chunk = [Math]::Min($step, $copyLen - $i)
        [System.Buffer]::BlockCopy($inBytes, $i, $outBytes, $i, $chunk)
        $percent = [Math]::Round(($i + $chunk) / $copyLen * 100, 1)
        $barLen = 20
        $filled = [Math]::Floor($barLen * $percent / 100)
        $bar = ('#' * $filled) + ('-' * ($barLen - $filled))
        $msg = "Progress: $bar $percent% ($i/$copyLen bytes)"
        Write-Host $msg -ForegroundColor Yellow; $msg | Out-File -Append $logFile
    }
    Write-Host "  - Buffer copy complete" -ForegroundColor Gray
    $outDir = Split-Path $OutputImage -Parent
    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
    [System.IO.File]::WriteAllBytes($OutputImage, $outBytes)
    Write-Host "  - Image file written: $OutputImage" -ForegroundColor Gray
    Write-Host "[2/3] Image creation complete ($TargetSize bytes)" -ForegroundColor Green
}
if (-not $SkipBoot) {
    if (-not (Test-Path $BootImagePath)) {
        Write-Host "ERROR: bootimage not found: $BootImagePath" -ForegroundColor Red
        throw "Build PC9801/src/boot (make) first or use -SkipBoot."
    }
}
if (-not $SkipItron) {
    if (-not (Test-Path $ItronImagePath)) {
        Write-Host "ERROR: itron.image not found: $ItronImagePath" -ForegroundColor Red
        throw "Build PC9801/src/kernel/itron-3.0/make (make) first or use -SkipItron."
    }
}
if (-not $SkipBoot) {
    $size = Get-TargetSizeBytes -Format $Format
    New-Pc98ImageFromFile -InputFile $BootImagePath -OutputImage $BootOut -TargetSize $size
}
if (-not $SkipItron) {
    $size = Get-TargetSizeBytes -Format $Format
    New-Pc98ImageFromFile -InputFile $ItronImagePath -OutputImage $ItronOut -TargetSize $size
}
if ($Run) {
    $images = @()
    if (-not $SkipBoot)  { $images += $BootOut }
    if (-not $SkipItron) { $images += $ItronOut }
    if ($images.Count -lt 1) {
        throw "-Run requires at least one image. Generate images or remove -Skip*."
    }
    $dosbox = "C:\dosbox-x.exe"
    $bootCmd = 'boot ' + ($images | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $args = @('-set', 'machine=pc98', '-c', $bootCmd)
    if (-not $NoExit) { $args += @('-c', 'exit') }
    Write-Host "Launching DOSBox-X (pc98) for boot check..." -ForegroundColor Cyan
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $dosbox
    $psi.Arguments = ($args -join ' ')
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit()
}
if (-not $SkipBoot -and -not $SkipItron) {
    Write-Host "Done. A: $BootOut / B: $ItronOut" -ForegroundColor Green
} elseif (-not $SkipBoot) {
    Write-Host "Done. A: $BootOut" -ForegroundColor Green
} elseif (-not $SkipItron) {
    Write-Host "Done. B: $ItronOut" -ForegroundColor Green
} else {
    Write-Host "No output due to skip switches." -ForegroundColor Yellow
}
    $p.WaitForExit()
