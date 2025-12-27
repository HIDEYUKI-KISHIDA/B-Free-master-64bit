<#
PC-98 OS自動化スクリプト
- イメージ検証・修復
- ビルド失敗時の自動リカバリ
- DOSBox-Xで可視化ブート
- パッケージング（ZIP, README, チェックサム, HTMLレポート）
- エラー時は詳細ログ出力
#>

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$imgA = "$scriptDir\..\src\boot\bootimage"
$imgB = "$scriptDir\..\src\kernel\itron-3.0\make\itron.image"
$rawA = "$scriptDir\pc98_boot.img"
$rawB = "$scriptDir\pc98_itron.img"
$dosboxExe = "C:\DOSBOX-X\dosbox-x.exe"
$zipOut = "$scriptDir\..\pc98_os_package.zip"
$readme = "$scriptDir\..\README.md"
$report = "$scriptDir\..\boot_report.html"

function Write-Log($msg) { Write-Host "[AUTOBOOT] $msg" }

Write-Log "=== PC-98 OS自動化開始 ==="

# 1. イメージ検証・修復
function Check-Image($img, $size) {
    if (!(Test-Path $img)) { Write-Log "$img がありません。"; return $false }
    $actual = (Get-Item $img).Length
    if ($actual -ne $size) {
        Write-Log "$img サイズ不一致: $actual / $size"
        return $false
    }
    return $true
}

$okA = Check-Image $imgA 1261568
$okB = Check-Image $imgB 1261568

if (-not ($okA -and $okB)) {
    Write-Log "イメージ再生成: create_pc98_floppy_images.ps1"
    & "$scriptDir\create_pc98_floppy_images.ps1"
    $okA = Check-Image $imgA 1261568
    $okB = Check-Image $imgB 1261568
    if (-not ($okA -and $okB)) {
        Write-Log "イメージ生成失敗。ビルドを確認してください。"
        exit 1
    }
}

# 2. RAWイメージ生成
Copy-Item $imgA $rawA -Force
Copy-Item $imgB $rawB -Force

# 3. DOSBox-Xで可視化ブート
Write-Log "DOSBox-XでOS起動テスト"
if (!(Test-Path $dosboxExe)) {
    Write-Log "DOSBox-Xが見つかりません: $dosboxExe"
    exit 2
}
Start-Process -FilePath $dosboxExe -ArgumentList "-machine pc98 -floppy $rawA -floppy2 $rawB" -Wait

# 4. パッケージング
Write-Log "パッケージング開始"
$files = @($rawA, $rawB, "$scriptDir\boot_pc98.cmd", "$scriptDir\run_pc98_dosboxx.ps1", $readme)
Compress-Archive -Path $files -DestinationPath $zipOut -Force

# 5. チェックサム生成
$shaA = (Get-FileHash $rawA -Algorithm SHA256).Hash
$shaB = (Get-FileHash $rawB -Algorithm SHA256).Hash
Set-Content -Path "$scriptDir\..\SHA256.txt" -Value "pc98_boot.img: $shaA`npc98_itron.img: $shaB"

# 6. HTMLレポート生成
$html = @"
<html><body>
<h2>PC-98 OS Boot Report</h2>
<ul>
<li>boot.img: $shaA</li>
<li>itron.img: $shaB</li>
<li>パッケージ: $zipOut</li>
</ul>
</body></html>
"@
Set-Content -Path $report -Value $html

Write-Log "=== 完了 ==="