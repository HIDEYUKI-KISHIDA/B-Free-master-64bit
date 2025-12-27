param(
    [string]$ISO = "bfree-32bit-bootable.iso",
    [string]$Image = "",
    [ValidateSet('cdrom','floppy','hd')]
    [string]$BootFrom = 'cdrom',
    [int]$Mem = 64,
    [int]$WaitSec = 6,
    [int]$MonitorPort = 4444,
    [int]$QmpPort = 5959,
    [ValidateSet('tcp','stdio')]
    [string]$MonitorMode = 'tcp',
    [ValidateSet('hmp','qmp')]
    [string]$Control = 'qmp',
    [ValidateSet('sdl','gtk','default','vnc')]
    [string]$Display = 'default',
    [string]$VncAddr = '127.0.0.1',
    [int]$VncIndex = 1
)

$ErrorActionPreference = "Stop"

# スクリプト配置ディレクトリを基準に動作させる
$ScriptDir = Split-Path -Parent $PSCommandPath
Set-Location $ScriptDir

# 起動元イメージの決定（優先: -Image 指定 > 既存の ISO 引数）
$mediaPath = $Image
if ([string]::IsNullOrWhiteSpace($mediaPath)) { $mediaPath = $ISO }
if (-not (Test-Path $mediaPath)) {
    Write-Error "Boot media not found: $mediaPath"
    exit 1
}

$shotDir = Join-Path (Get-Location) "screenshots"
New-Item -ItemType Directory -Force -Path $shotDir | Out-Null
$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$shotFile = Join-Path $shotDir ("boot-" + $ts + ".ppm")
$shotFileAbs = [System.IO.Path]::GetFullPath($shotFile)
# QEMU HMP は Windows のバックスラッシュやコロンを嫌う場合があるため、相対パス/スラッシュを優先
$shotRel = Join-Path "screenshots" ("boot-" + $ts + ".ppm")
$shotRel = ($shotRel -replace '\\','/')

# 起動: HMPモニタを TCP で受け付ける
$qemuArgs = @(
    "-m", $Mem,
    "-vga", "std",
    "-no-reboot",
    "-no-shutdown"
)

switch ($BootFrom) {
    'cdrom' {
        $qemuArgs += @("-cdrom", $mediaPath, "-boot", "d")
    }
    'floppy' {
        # フロッピー（FAT/RAW）想定
        $qemuArgs += @("-fda", $mediaPath, "-boot", "a")
    }
    'hd' {
        # ハードディスクRAWイメージ想定（IDE）
        $qemuArgs += @("-drive", ("file=" + $mediaPath + ",format=raw,if=ide"), "-boot", "c")
    }
}

if ($Display -eq 'vnc') {
    $qemuArgs += @("-display", ("vnc=" + $VncAddr + ":" + $VncIndex))
} elseif ($Display -ne 'default') {
    $qemuArgs += @("-display", $Display)
}

if ($Control -eq 'hmp') {
    if ($MonitorMode -eq 'tcp') {
        $qemuArgs += @("-monitor", ("tcp:127.0.0.1:" + $MonitorPort + ",server,nowait"))
    } else {
        $qemuArgs += @("-monitor", "stdio")
    }
} else {
    # QMP (JSON) で制御
    $qemuArgs += @("-qmp", ("tcp:127.0.0.1:" + $QmpPort + ",server,nowait"))
}

Write-Host "Launching QEMU..." -ForegroundColor Cyan
$proc = Start-Process -FilePath "qemu-system-i386" -ArgumentList $qemuArgs -PassThru -WindowStyle Normal -WorkingDirectory $ScriptDir

# ポート待機（HMP/TCP または QMP の場合のみ）
$portToWait = $null
if ($Control -eq 'qmp') { $portToWait = $QmpPort }
elseif ($Control -eq 'hmp' -and $MonitorMode -eq 'tcp') { $portToWait = $MonitorPort }

if ($portToWait) {
    $deadline = (Get-Date).AddSeconds(10)
    $connected = $false
    while ((Get-Date) -lt $deadline) {
        try {
            $client = New-Object System.Net.Sockets.TcpClient
            $iar = $client.BeginConnect("127.0.0.1", $portToWait, $null, $null)
            $ok = $iar.AsyncWaitHandle.WaitOne(500)
            if ($ok -and $client.Connected) {
                $client.EndConnect($iar)
                $connected = $true
                break
            }
            $client.Close()
        } catch { Start-Sleep -Milliseconds 200 }
    }
    if (-not $connected) {
        Write-Warning "Control port not ready; continuing anyway"
    }
}

# ブート待ち
Start-Sleep -Seconds $WaitSec

if ($Control -eq 'hmp') {
    if ($MonitorMode -eq 'tcp') {
        # HMP (TCP)
        try {
            if (-not $client) { $client = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $MonitorPort) }
            $stream = $client.GetStream()
            $writer = New-Object System.IO.StreamWriter($stream); $writer.NewLine="\n"; $writer.AutoFlush=$true
            try { if ($stream.DataAvailable) { $buf = New-Object byte[] 4096; $null = $stream.Read($buf,0,$buf.Length) } } catch { }
            if (-not (Test-Path $shotDir)) { New-Item -ItemType Directory -Force -Path $shotDir | Out-Null }
            $cmd = "screendump " + $shotRel
            Write-Host "HMP> $cmd" -ForegroundColor Yellow
            $writer.WriteLine($cmd)
            $deadline2 = (Get-Date).AddSeconds(7)
            while ((Get-Date) -lt $deadline2 -and -not (Test-Path $shotFile)) { Start-Sleep -Milliseconds 200 }
            Start-Sleep -Milliseconds 300
            $writer.WriteLine("quit"); Start-Sleep -Milliseconds 300
            $writer.Dispose(); $stream.Dispose(); $client.Close()
        } catch { Write-Warning "Failed to send screendump/quit (tcp): $_" }
    } else {
        # HMP (-monitor stdio)
        try {
            $stdin = $proc.StandardInput
            $cmd = "screendump " + $shotRel
            Write-Host "HMP(stdio)> $cmd" -ForegroundColor Yellow
            $stdin.WriteLine($cmd); $stdin.Flush()
            $deadline2 = (Get-Date).AddSeconds(7)
            while ((Get-Date) -lt $deadline2 -and -not (Test-Path $shotFile)) { Start-Sleep -Milliseconds 200 }
            Start-Sleep -Milliseconds 300
            $stdin.WriteLine("quit"); $stdin.Flush(); Start-Sleep -Milliseconds 300
        } catch { Write-Warning "Failed to send screendump/quit (stdio): $_" }
    }
} else {
    # QMP JSONで screendump 実行
    try {
        $deadline = (Get-Date).AddSeconds(15)
        $qmpReady = $false
        while ((Get-Date) -lt $deadline -and -not $qmpReady) {
            try { $qmp = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $QmpPort); $qmpReady = $true } catch { Start-Sleep -Milliseconds 300 }
        }
        if (-not $qmpReady) { throw "QMP port not ready" }
        $qs = $qmp.GetStream(); $sr = New-Object System.IO.StreamReader($qs); $sw = New-Object System.IO.StreamWriter($qs); $sw.NewLine = "\n"; $sw.AutoFlush = $true
        # Read greeting
        Start-Sleep -Milliseconds 300
        if ($qs.DataAvailable) { $null = $sr.ReadLine() }
        # capabilities
        $sw.WriteLine('{"execute":"qmp_capabilities"}')
        Start-Sleep -Milliseconds 200
        if (-not (Test-Path $shotDir)) { New-Item -ItemType Directory -Force -Path $shotDir | Out-Null }
        $qmpPath = $shotRel
        $json = '{"execute":"screendump","arguments":{"filename":"' + $qmpPath + '"}}'
        Write-Host "QMP> screendump to $shotFileAbs" -ForegroundColor Yellow
        $sw.WriteLine($json)
        $deadline2 = (Get-Date).AddSeconds(7)
        while ((Get-Date) -lt $deadline2 -and -not (Test-Path $shotFile)) { Start-Sleep -Milliseconds 200 }
        $sw.WriteLine('{"execute":"quit"}')
        Start-Sleep -Milliseconds 200
        $sr.Dispose(); $sw.Dispose(); $qs.Dispose(); $qmp.Close()
    } catch { Write-Warning "Failed to send screendump/quit (qmp): $_" }
}

# 終了待機
try { Wait-Process -Id $proc.Id -Timeout 5 } catch {}

if (Test-Path $shotFile) {
    $fi = Get-Item $shotFile
    Write-Host ("Saved screenshot: " + $fi.FullName + " (" + $fi.Length + " bytes)") -ForegroundColor Green
    exit 0
} else {
    Write-Warning "Screenshot not found yet. Listing directory: $shotDir"
    Get-ChildItem $shotDir | Format-Table Name,Length,LastWriteTime
    exit 2
}
