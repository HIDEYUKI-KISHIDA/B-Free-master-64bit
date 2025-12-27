param(
    [string]$BootImage = "c:\\Users\\h_kis\\Desktop\\B-Free-master\\Program\\PC9801\\tools\\pc98_boot.img",
    [string]$ItronImage = "c:\\Users\\h_kis\\Desktop\\B-Free-master\\Program\\PC9801\\tools\\pc98_itron.img",
    [string]$DosboxXExe,
    [int]$MemMB = 8,
    [switch]$AllowPartial,
    [string]$ConfPath
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
    # Additional common roots on C:\
    $roots = @(
        'C:\\dosbox-x', 'C:\\DOSBox-X',
        'C:\\Tools\\dosbox-x', 'C:\\Tools\\DOSBox-X',
        'C:\\Apps\\dosbox-x', 'C:\\Apps\\DOSBox-X',
        'C:\\Portable\\dosbox-x', 'C:\\Portable\\DOSBox-X',
        'C:\\ProgramData\\dosbox-x', 'C:\\ProgramData\\DOSBox-X'
    )
    foreach ($r in $roots) {
        if ($r -and (Test-Path $r)) {
            $exe = Join-Path $r 'dosbox-x.exe'
            if (Test-Path $exe) { return $exe }
            $found = Get-ChildItem -Path $r -Filter 'dosbox-x.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($found) { return $found.FullName }
        }
    }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    $regPaths = @(
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    foreach ($rp in $regPaths) {
        try {
            $items = Get-ItemProperty -Path $rp -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -like '*DOSBox-X*' }
            foreach ($it in $items) {
                if ($it.InstallLocation -and (Test-Path $it.InstallLocation)) {
                    $exe = Join-Path $it.InstallLocation 'dosbox-x.exe'
                    if (Test-Path $exe) { return $exe }
                    $found = Get-ChildItem -Path $it.InstallLocation -Filter 'dosbox-x.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
                    if ($found) { return $found.FullName }
                }
                if ($it.UninstallString) {
                    $guess = ($it.UninstallString -replace '"','').Split(' ')
                    $exe = $guess | Where-Object { $_ -like '*dosbox-x.exe' }
                    if ($exe) { $pp = $exe | Select-Object -First 1; if (Test-Path $pp) { return $pp } }
                }
            }
        } catch { }
    }
    throw "dosbox-x not found. Add to PATH or specify -DosboxXExe."
}

if (-not (Test-Path $BootImage)) {
    if (-not $AllowPartial) { throw "Boot image missing: $BootImage" }
}
if (-not (Test-Path $ItronImage)) {
    if (-not $AllowPartial) { throw "ITRON image missing: $ItronImage" }
}

$dosbox = Resolve-DosboxX -Path $DosboxXExe

# pc98 モードで A/B の 1.23MB イメージから起動
# メモリは小さめで十分。必要なら -MemMB で増やす。
$args = @(
    '-set', 'machine=pc98',
    '-set', "memsize=$MemMB"
)

# Optional: configure captures directory via a conf file
if (-not $ConfPath -or [string]::IsNullOrWhiteSpace($ConfPath)) {
    $toolDir = Split-Path -Parent $PSCommandPath
    $capDir = Join-Path $toolDir 'captures'
    if (-not (Test-Path $capDir)) { New-Item -ItemType Directory -Path $capDir | Out-Null }
    $ConfPath = Join-Path $toolDir 'dosbox-x.conf'
    $conf = "[dosbox]" + [Environment]::NewLine + "captures=$capDir" + [Environment]::NewLine
    Set-Content -Path $ConfPath -Value $conf -Encoding ASCII
}
if (Test-Path $ConfPath) { $args += @('-conf', $ConfPath) }

# Compose boot command with available images
$bootList = @()
if (Test-Path $BootImage) { $bootList += $BootImage }
if (Test-Path $ItronImage) { $bootList += $ItronImage }
if ($bootList.Count -eq 0) { throw "No boot media available. Provide BootImage and/or ItronImage." }
$bootCmd = 'boot ' + (( $bootList | ForEach-Object { '"' + $_ + '"' } ) -join ' ')
$args += @('-c', $bootCmd, '-c', 'exit')

Write-Host "Start DOSBox-X(pc98): " -ForegroundColor Cyan
Write-Host $bootCmd -ForegroundColor Yellow
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $dosbox
$psi.Arguments = ($args -join ' ')
$psi.UseShellExecute = $false
$p = [System.Diagnostics.Process]::Start($psi)
$p.WaitForExit()
