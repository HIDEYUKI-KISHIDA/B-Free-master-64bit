param(
    [string]$DosboxXExe,
    [switch]$NoExit
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-DosboxX {
    param([string]$Path)
    if ($Path) { return $Path }
    $cmd = Get-Command dosbox-x -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Path }
    $candidates = @(
        (Join-Path $env:ProgramFiles        'dosbox-x\dosbox-x.exe'),
        (Join-Path $env:ProgramFiles        'DOSBox-X\dosbox-x.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'dosbox-x\dosbox-x.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'DOSBox-X\dosbox-x.exe'),
        (Join-Path $env:LOCALAPPDATA        'Programs\dosbox-x\dosbox-x.exe'),
        (Join-Path $env:LOCALAPPDATA        'DOSBox-X\dosbox-x.exe')
    )
    $roots = @(
        'C:\dosbox-x','C:\DOSBox-X',
        'C:\Tools\dosbox-x','C:\Tools\DOSBox-X',
        'C:\Apps\dosbox-x','C:\Apps\DOSBox-X',
        'C:\Portable\dosbox-x','C:\Portable\DOSBox-X',
        'C:\ProgramData\dosbox-x','C:\ProgramData\DOSBox-X'
    )
    foreach ($r in $roots) {
        if ($r -and (Test-Path $r)) {
            $exe = Join-Path $r 'dosbox-x.exe'
            if (Test-Path $exe) { return $exe }
            $found = Get-ChildItem -Path $r -Filter 'dosbox-x.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($found) { return $found.FullName }
        }
    }
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    $proc = Get-CimInstance Win32_Process -Filter "name='dosbox-x.exe'" | Select-Object -First 1
    if ($proc -and $proc.ExecutablePath -and (Test-Path $proc.ExecutablePath)) { return $proc.ExecutablePath }
    throw 'dosbox-x not found. Provide -DosboxXExe.'
}

function Get-MSYS2BashPath {
    $paths = @('C:\msys64\usr\bin\bash.exe','C:\Program Files\msys64\usr\bin\bash.exe')
    foreach ($p in $paths) { if (Test-Path $p) { return $p } }
    $desktop = [Environment]::GetFolderPath('Desktop')
    $inst = Get-ChildItem -Path $desktop -Filter 'msys2*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $inst) { $inst = Get-ChildItem -Path $desktop -Filter '*.exe' -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'msys2|msys' } | Select-Object -First 1 }
    if ($inst) {
        Write-Host ("Installing MSYS2 from: " + $inst.FullName) -ForegroundColor Cyan
        try {
            Start-Process -FilePath $inst.FullName -ArgumentList '/VERYSILENT','/NORESTART','/DIR=C:\msys64' -Wait
        } catch {
            Write-Warning 'Silent install failed. Please run the installer manually and retry.'
        }
    }
    foreach ($p in $paths) { if (Test-Path $p) { return $p } }
    throw 'MSYS2 bash not found.'
}

function Ensure-Toolchain([string]$bash) {
    & $bash -lc 'pacman -Sy --noconfirm'
    & $bash -lc 'pacman -S --noconfirm base-devel gcc binutils make'
}

function Build-Targets([string]$bash) {
    & $bash -lc 'cd /c/Users/h_kis/Desktop/B-Free-master/Program/PC9801/src/boot; make clean; make image'
    & $bash -lc 'cd /c/Users/h_kis/Desktop/B-Free-master/Program/PC9801/src/kernel/itron-3.0/make; make'
}

function Run-DosboxX([string]$dosboxExe,[string[]]$images,[switch]$NoExit) {
    $args = @('-set','machine=pc98','-c',('boot ' + (($images | ForEach-Object { '"{0}"' -f $_ }) -join ' ')))
    if (-not $NoExit) { $args += @('-c','exit') }
    Write-Host 'Launching DOSBox-X (pc98)...' -ForegroundColor Cyan
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $dosboxExe
    $psi.Arguments = ($args -join ' ')
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit()
}

$bash = Get-MSYS2BashPath
Ensure-Toolchain -bash $bash
Build-Targets -bash $bash

$toolDir = Split-Path -Parent $PSCommandPath
$scriptPath = Join-Path $toolDir 'create_pc98_floppy_images.ps1'
if (-not (Test-Path $scriptPath)) { throw "Missing script: $scriptPath" }

$dosbox = Resolve-DosboxX -Path $DosboxXExe
Write-Host "DOSBox-X: $dosbox" -ForegroundColor Yellow

Write-Host 'Creating PC-98 floppy images...' -ForegroundColor Cyan
PowerShell -ExecutionPolicy Bypass -File $scriptPath

$bootImg  = Join-Path $toolDir 'pc98_boot.img'
$itronImg = Join-Path $toolDir 'pc98_itron.img'
$images = @()
if (Test-Path $bootImg)  { $images += $bootImg }
if (Test-Path $itronImg) { $images += $itronImg }
if ($images.Count -lt 1) { throw 'No images created. Build may have failed.' }

Run-DosboxX -dosboxExe $dosbox -images $images -NoExit:$NoExit

Write-Host 'Done.' -ForegroundColor Green
