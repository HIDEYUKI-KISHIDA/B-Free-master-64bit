param(
    [string]$BtronRoot,
    [int]$MaxPerYear = 5
)

# Resolve default btron-pc root relative to this script (..\..\btron-pc)
if (-not $BtronRoot -or -not (Test-Path $BtronRoot)) {
    $repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    $BtronRoot = Join-Path $repoRoot "btron-pc"
}

if (-not (Test-Path $BtronRoot)) {
    Write-Host "btron-pc root not found: $BtronRoot"
    exit 1
}

Write-Host "Scanning btron-pc under: $BtronRoot"

$yearFiles = @{}

function Add-YearHit([int]$year, [string]$filePath) {
    if ($year -lt 1985 -or $year -gt 2035) { return }
    $key = [string]$year
    if (-not $yearFiles.ContainsKey($key)) {
        $yearFiles[$key] = New-Object System.Collections.Generic.HashSet[string]
    }
    # store path relative to btron-pc
    $rel = $filePath.Substring($BtronRoot.Length).TrimStart([char[]]"/\")
    $rel = ($rel -replace '\\','/')
    [void]$yearFiles[$key].Add($rel)
}

$files = Get-ChildItem -Path $BtronRoot -Recurse -File -ErrorAction SilentlyContinue |
         Where-Object { $_.Extension -notin '.o','.obj','.exe','.bin','.img','.iso' }

foreach ($f in $files) {
    try {
        $head = Get-Content -Path $f.FullName -TotalCount 80 -ErrorAction Stop -Encoding UTF8
    } catch {
        # fall back: try default encoding without specifying
        try { $head = Get-Content -Path $f.FullName -TotalCount 80 -ErrorAction Stop } catch { continue }
    }
    foreach ($line in $head) {
        # Pattern 1: Version ... Month YYYY
        if ($line -match '(?i)Version\s+\d+.*?(January|February|March|April|May|June|July|August|September|October|November|December)\s+(?<y>\d{4})') {
            Add-YearHit -year ([int]$Matches.y) -filePath $f.FullName
            continue
        }
        # Pattern 2: $Header: ... YYYY/MM/DD ...
        if ($line -match '\$Header: .*? (?<y>\d{4})[/-](?<m>\d{1,2})[/-](?<d>\d{1,2})') {
            Add-YearHit -year ([int]$Matches.y) -filePath $f.FullName
            continue
        }
        # Pattern 2b: Copyright line with one or more years
        if ($line -match '(?i)copyright') {
            $yearsInLine = [regex]::Matches($line, '(19|20)\d{2}')
            foreach ($m in $yearsInLine) {
                $yy = [int]$m.Value
                Add-YearHit -year $yy -filePath $f.FullName
            }
            if ($yearsInLine.Count -gt 0) { continue }
        }
        # Pattern 3: plain YYYY in header comments
        if ($line -match '\b(19|20)\d{2}\b') {
            $y = [int]([regex]::Match($line, '(19|20)\d{2}').Value)
            Add-YearHit -year $y -filePath $f.FullName
            continue
        }
    }
}

# Build markdown
$outPath = Join-Path $PSScriptRoot 'btronpc_timeline.md'
$years = ($yearFiles.Keys | Sort-Object {[int]$_})
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('### btron-pc timeline (auto-extracted)')
[void]$sb.AppendLine('')
foreach ($yk in $years) {
    $list = @($yearFiles[$yk] | Sort-Object) | Select-Object -First $MaxPerYear
    if ($list.Count -eq 0) { continue }
    [void]$sb.AppendLine('- ' + $yk)
    foreach ($rel in $list) {
        $line = "  - btron-pc$rel"
        [void]$sb.AppendLine($line)
    }
}

Set-Content -Path $outPath -Value ($sb.ToString()) -Encoding UTF8
Write-Host "Wrote: $outPath"