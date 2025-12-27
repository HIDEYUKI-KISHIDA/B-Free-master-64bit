param(
    [switch]$Rebuild,
    [int]$Mem = 64,
    [int]$WaitSec = 10,
    [ValidateSet('qmp','hmp')]
    [string]$Control = 'qmp',
    [ValidateSet('win32','sdl','gtk','default','vnc')]
    [string]$Display = 'win32'
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $PSCommandPath
Set-Location $ScriptDir

if ($Rebuild) {
    Write-Host "Rebuilding 2ndboot and ISO..." -ForegroundColor Cyan
    & make clean | Out-Null
    & make USE_MINGW32=1 mode32
    if ($LASTEXITCODE -ne 0) { throw "make mode32 failed" }
    & make USE_MINGW32=1 2ndboot
    if ($LASTEXITCODE -ne 0) { throw "make 2ndboot failed" }
    powershell -ExecutionPolicy Bypass -File .\create_bootable_iso.ps1 -BootImagePath 2ndboot -OutputISO bfree-32bit-bootable.iso
}

# 利用可能な起動メディア候補（自動検出: ISO → フロッピー → 既知RAW）
$iso = Join-Path $ScriptDir 'bfree-32bit-bootable.iso'
$fda1 = Join-Path $ScriptDir 'bootdisk.img'
$fda2 = Join-Path $ScriptDir 'bootable.img'

$sources = @()
if (Test-Path $iso)  { $sources += @{ Path = $iso;  BootFrom = 'cdrom'  } }
if (Test-Path $fda1) { $sources += @{ Path = $fda1; BootFrom = 'floppy' } }
if (Test-Path $fda2) { $sources += @{ Path = $fda2; BootFrom = 'floppy' } }

if ($sources.Count -eq 0) { throw "No boot media found (ISO/floppy). Build or provide an image." }

Write-Host "Launching QEMU and capturing screenshot..." -ForegroundColor Cyan

# 直近のPPMのタイムスタンプを覚えておく（新規生成検出用）
$shotDir = Join-Path $ScriptDir 'screenshots'
New-Item -ItemType Directory -Force -Path $shotDir | Out-Null
$before = Get-ChildItem $shotDir -Filter *.ppm -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1

# フォールバック戦略の順序（より成功しやすいものから）
$strategies = @(
    @{ Control = 'qmp';  Display = 'vnc';   MonitorMode = 'tcp' },
    @{ Control = 'qmp';  Display = 'win32'; MonitorMode = 'tcp' },
    @{ Control = 'qmp';  Display = 'sdl';   MonitorMode = 'tcp' },
    @{ Control = 'hmp';  Display = 'vnc';   MonitorMode = 'tcp' },
    @{ Control = 'hmp';  Display = 'win32'; MonitorMode = 'tcp' },
    @{ Control = 'hmp';  Display = 'sdl';   MonitorMode = 'tcp' },
    @{ Control = 'hmp';  Display = 'gtk';   MonitorMode = 'tcp' },
    @{ Control = 'hmp';  Display = 'win32'; MonitorMode = 'stdio' }
)

$ppm = $null
foreach ($src in $sources) {
    foreach ($s in $strategies) {
        Write-Host ("Trying: Media=" + $src.Path + " (" + $src.BootFrom + "), Control=" + $s.Control + ", Display=" + $s.Display + ", MonitorMode=" + $s.MonitorMode) -ForegroundColor DarkCyan
        try {
            $args = @(
                '-ExecutionPolicy','Bypass','-File','./capture_boot_screenshot.ps1',
                '-Image',$src.Path,'-BootFrom',$src.BootFrom,
                '-Mem',$Mem,'-WaitSec',$WaitSec,
                '-Display',$s.Display,'-Control',$s.Control
            )
            if ($s.Control -eq 'hmp') { $args += @('-MonitorMode',$s.MonitorMode) }
            powershell @args | Out-Null
        } catch {
            Write-Warning ("Attempt failed: " + $_)
        }

        # 新規PPM検出
        $after = Get-ChildItem $shotDir -Filter *.ppm -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($after -and ((-not $before) -or ($after.LastWriteTime -gt $before.LastWriteTime))) {
            $ppm = $after
            break
        }
    }
    if ($ppm) { break }
}

if (-not $ppm) { throw "Failed to capture screenshot via all strategies. Check QEMU installation and display backends." }

Write-Host ("Converting to PNG: " + $ppm.FullName) -ForegroundColor Yellow
powershell -ExecutionPolicy Bypass -File .\ppm_to_png.ps1 -InputPpm $ppm.FullName

$png = [System.IO.Path]::ChangeExtension($ppm.FullName, '.png')
if (Test-Path $png) {
    Write-Host ("PNG saved: " + $png) -ForegroundColor Green
    $latest = Join-Path $shotDir 'boot-latest.png'
    Copy-Item -Force -Path $png -Destination $latest
    Write-Host ("Latest PNG: " + $latest) -ForegroundColor Green
} else {
    Write-Warning "PNG conversion did not produce expected output."
}

Write-Host "Done." -ForegroundColor Green
