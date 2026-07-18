# tools/_oss_undef_collect.ps1
# -----------------------------------------------------------------------------
# OSS 通関リスト生成スクリプト
#
# 役割:
#   busybox / bash / python3 / dropbear / weston-terminal 等の OSS バイナリから
#   `nm -u` で未定義参照シンボルを抽出し、POSIX 685 関数リストと突合して
#   「実際に呼ばれている POSIX 関数」のみを抽出する。
#
# 出力:
#   - tools/oss_undef/<binary>.undef.txt    : nm -u の生結果
#   - tools/oss_undef/<binary>.posix.txt    : POSIX 685 との交差
#   - POSIX_OSS_GATEWAY_PRIORITY.csv        : 全 OSS 横断の優先度リスト
#
# 使い方:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\_oss_undef_collect.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\_oss_undef_collect.ps1 -BinaryDir D:\oss_bins
#
# 前提:
#   - WSL or MSYS2 の `nm` が PATH に通っていること（または -NmPath で指定）
#   - 既定の -BinaryDir は Program/bfree_x86_64/tests/oss_bins/
#     （存在しない場合はディレクトリのみ作成して終了）
#
# 戦略文書: POSIX_OSS_GATEWAY_STRATEGY.md §2
# -----------------------------------------------------------------------------

[CmdletBinding()]
param(
    [string]$BinaryDir = "",
    [string]$NmPath    = "nm",
    [string]$OutDir    = "",
    [switch]$Quiet
)

$ErrorActionPreference = "Continue"

# ルート（このスクリプトは Program/bfree_x86_64/tools/ にある想定）
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

if (-not $BinaryDir) {
    $BinaryDir = Join-Path $root "tests\oss_bins"
}
if (-not $OutDir) {
    $OutDir = Join-Path $root "tools\oss_undef"
}

# 出力ディレクトリ用意
if (-not (Test-Path $BinaryDir)) {
    New-Item -ItemType Directory -Path $BinaryDir -Force | Out-Null
    Write-Host "[oss_undef] created binary dir (empty): $BinaryDir"
}
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

# POSIX 685 シンボル集合
$posixList = Join-Path $root "posix2017_functions_only.txt"
if (-not (Test-Path $posixList)) {
    Write-Error "posix2017_functions_only.txt not found at $posixList"
    exit 2
}
$posixSet = New-Object System.Collections.Generic.HashSet[string]
foreach ($line in Get-Content $posixList) {
    $s = $line.Trim()
    if (-not $s) { continue }
    if ($s -match '^(Requirements|Threads|Flags)\s*$') { continue }
    if ($s -match '^[A-Za-z_][A-Za-z0-9_]*$') { [void]$posixSet.Add($s) }
}
Write-Host "[oss_undef] POSIX 685 symbols loaded: $($posixSet.Count)"

# 対象バイナリ列挙
$bins = Get-ChildItem -Path $BinaryDir -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -notin @('.txt', '.md', '.csv', '.log') }

if (-not $bins -or $bins.Count -eq 0) {
    Write-Host "[oss_undef] WARN: no binaries under $BinaryDir"
    Write-Host "[oss_undef]   place ELF binaries here (busybox, bash, python3, ...)"
    Write-Host "[oss_undef]   then re-run this script."
    exit 0
}

# nm が動くか確認
try {
    $nmTest = & $NmPath --version 2>&1
    if ($LASTEXITCODE -ne 0) { throw "nm not runnable" }
} catch {
    Write-Error "nm command not available ($NmPath). Install binutils / WSL or pass -NmPath."
    exit 3
}

# CSV ヘッダ
$csvPath = Join-Path $root "POSIX_OSS_GATEWAY_PRIORITY.csv"
$csvLines = @("symbol,used_by,used_count")

# シンボル→使用 OSS マップ
$symMap = @{}  # symbol -> [System.Collections.Generic.HashSet[string]]

foreach ($bin in $bins) {
    Write-Host "[oss_undef] scanning $($bin.Name) ..."
    $undefRaw = & $NmPath -u $bin.FullName 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $undefRaw) {
        Write-Host "  -> nm -u failed or empty (skipped)"
        continue
    }
    # 例: "                 U printf@@GLIBC_2.2.5"  ->  printf
    $undefSyms = $undefRaw |
        ForEach-Object {
            $tok = ($_ -split '\s+') | Where-Object { $_ -ne '' } | Select-Object -Last 1
            if ($tok) { ($tok -split '@')[0] } else { $null }
        } |
        Where-Object { $_ -and ($_ -match '^[A-Za-z_][A-Za-z0-9_]*$') } |
        Sort-Object -Unique

    $undefFile = Join-Path $OutDir ("$($bin.BaseName).undef.txt")
    $undefSyms | Set-Content -Path $undefFile -Encoding ascii

    $posixHit = $undefSyms | Where-Object { $posixSet.Contains($_) }
    $posixFile = Join-Path $OutDir ("$($bin.BaseName).posix.txt")
    $posixHit | Set-Content -Path $posixFile -Encoding ascii

    Write-Host ("  -> undef={0}, posix_hit={1}" -f $undefSyms.Count, $posixHit.Count)

    foreach ($s in $posixHit) {
        if (-not $symMap.ContainsKey($s)) {
            $symMap[$s] = New-Object System.Collections.Generic.HashSet[string]
        }
        [void]$symMap[$s].Add($bin.BaseName)
    }
}

# CSV 出力（used_count 降順、シンボル昇順）
$rows = foreach ($k in $symMap.Keys) {
    [PSCustomObject]@{
        symbol     = $k
        used_by    = ($symMap[$k] | Sort-Object) -join ';'
        used_count = $symMap[$k].Count
    }
}
$rows = $rows | Sort-Object @{Expression='used_count';Descending=$true}, @{Expression='symbol';Descending=$false}

foreach ($r in $rows) {
    $csvLines += ("{0},{1},{2}" -f $r.symbol, $r.used_by, $r.used_count)
}
$csvLines | Set-Content -Path $csvPath -Encoding utf8

Write-Host ""
Write-Host "[oss_undef] DONE"
Write-Host "  per-binary undef : $OutDir\*.undef.txt"
Write-Host "  per-binary posix : $OutDir\*.posix.txt"
Write-Host "  priority CSV     : $csvPath"
Write-Host ("  total unique POSIX symbols used: {0}" -f $rows.Count)
