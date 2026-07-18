# POSIX 685 実装状況を機械的に集計するスクリプト
# 使い方:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\_impl_status_scan.ps1
#
# 出力:
#   - 標準出力にサマリ
#   - tools\impl_status_report.md にレポート

$ErrorActionPreference = "Continue"

# このスクリプトは Program/bfree_x86_64 直下に置く想定だが、tools/ から呼ぶので
# 親ディレクトリに移動する
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

$posixList = Join-Path $root "posix2017_functions_only.txt"
if (-not (Test-Path $posixList)) {
    Write-Error "posix2017_functions_only.txt not found at $posixList"
    exit 1
}

# 1) 対象シンボル 685 を抽出 (3 行のセクション見出しを除外)
$rawLines = Get-Content $posixList
$symbols  = $rawLines | Where-Object {
    $_ -and ($_ -notmatch '^(Requirements|Threads|Flags)\s*$')
} | ForEach-Object { $_.Trim() }

$total = $symbols.Count
Write-Host "Total target symbols: $total"

# 2) 検索対象ディレクトリ
$searchDirs = @(
    "userland\libc",
    "userland\libc\bfree_posix",
    "bfree\userland",
    "bfree\bin",
    "bfree\common",
    "bfree\ipc",
    "bfree\vfs",
    "bfree\manager",
    "bfree\server",
    "kernel"
) | Where-Object { Test-Path $_ }

# 3) 全ソースファイルを集めて、内容を 1 回だけ読み込む
$srcFiles = @()
foreach ($d in $searchDirs) {
    $srcFiles += Get-ChildItem -Path $d -Recurse -File `
        -Include *.c,*.h,*.S,*.s -ErrorAction SilentlyContinue
}
Write-Host ("Source files scanned: " + $srcFiles.Count)

# ファイル名インデックス (musl_libc_<sym>.c / <sym>.c など)
$nameSet = @{}
foreach ($f in $srcFiles) {
    $nameSet[$f.Name.ToLower()] = $true
}

# 関数定義検出のため、全ファイルから「識別子 (」のパターンを抽出する正規表現は
# 件数 x ファイル数で重いので、シンボル名集合と grep で 1 ファイル 1 回で済ます

# シンボルを正規表現エスケープして、|連結
$allSymsPattern = "\b(" + (($symbols | ForEach-Object { [regex]::Escape($_) }) -join "|") + ")\s*\("

$defRegex = [regex]::new($allSymsPattern, "Compiled")

# Per symbol counters
$hasDef = @{}        # 関数定義 (... sym(...) { )  を含むファイルがある
$hasFile = @{}       # ファイル名一致 (musl_libc_sym.c 等)
foreach ($s in $symbols) { $hasDef[$s] = $false; $hasFile[$s] = $false }

# ファイル名一致 (musl_libc_<sym>.c, bfree_<sym>.c, <sym>.c)
foreach ($s in $symbols) {
    $cands = @(
        ("musl_libc_" + $s + ".c"),
        ("bfree_"     + $s + ".c"),
        ($s + ".c")
    )
    foreach ($c in $cands) {
        if ($nameSet.ContainsKey($c.ToLower())) { $hasFile[$s] = $true; break }
    }
}

# 関数定義らしき出現を検出
# ヒューリスティック: 「行頭付近に <ret> <sym>(」 か、シンボル直前が空白/型/* 等で、{ が同行 or 次行
# ここでは緩めに「<sym>(」が同一ファイル内に登場し、かつそのファイルが
# ヘッダではなく .c/.S/.s であれば「定義あり候補」とする。
foreach ($f in $srcFiles) {
    if ($f.Extension -ieq ".h") { continue }
    try {
        $text = [IO.File]::ReadAllText($f.FullName)
    } catch { continue }
    foreach ($m in $defRegex.Matches($text)) {
        $sym = $m.Groups[1].Value
        if ($hasDef.ContainsKey($sym)) {
            # 直前文字が '.' や '->' なら除外 (構造体メンバ呼び出し)
            $idx = $m.Index
            $prev = if ($idx -gt 0) { $text[$idx - 1] } else { ' ' }
            if ($prev -ne '.' -and $prev -ne '>' ) {
                $hasDef[$sym] = $true
            }
        }
    }
}

# 4) 判定 (DONE: 定義あり or 専用ファイルあり / MISSING: 両方なし)
$done    = New-Object System.Collections.ArrayList
$file_only = New-Object System.Collections.ArrayList
$missing = New-Object System.Collections.ArrayList

foreach ($s in $symbols) {
    if ($hasDef[$s])      { [void]$done.Add($s); continue }
    if ($hasFile[$s])     { [void]$file_only.Add($s); continue }
    [void]$missing.Add($s)
}

Write-Host ""
Write-Host ("DONE (function body found) : " + $done.Count)
Write-Host ("FILE_ONLY (named .c only)  : " + $file_only.Count)
Write-Host ("MISSING                    : " + $missing.Count)
Write-Host ("TOTAL                      : " + $total)

# 5) ABCDE 分類とクロス集計 (POSIX_685_ABCDE.csv があれば)
$abcdeCsv = Join-Path $root "POSIX_685_ABCDE.csv"
$cat = @{}
$layer = @{}
if (Test-Path $abcdeCsv) {
    foreach ($line in Get-Content $abcdeCsv) {
        $parts = $line.Split(',')
        if ($parts.Count -ge 3) {
            $cat[$parts[0]]   = $parts[1]
            $layer[$parts[0]] = $parts[2]
        }
    }
}

# 集計テーブル
$summary = @{}
foreach ($s in $symbols) {
    $c = if ($cat.ContainsKey($s))   { $cat[$s] }   else { "?" }
    $l = if ($layer.ContainsKey($s)) { $layer[$s] } else { "?" }
    $key = "$c|$l"
    if (-not $summary.ContainsKey($key)) {
        $summary[$key] = @{ done=0; file_only=0; missing=0; total=0 }
    }
    $summary[$key].total++
    if     ($hasDef[$s])  { $summary[$key].done++ }
    elseif ($hasFile[$s]) { $summary[$key].file_only++ }
    else                   { $summary[$key].missing++ }
}

# 6) レポート出力
$report = New-Object System.Text.StringBuilder
[void]$report.AppendLine("# POSIX 685 実装状況スキャン結果")
[void]$report.AppendLine("")
[void]$report.AppendLine("- 生成日時: " + (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
[void]$report.AppendLine("- 対象シンボル数: $total")
[void]$report.AppendLine("- スキャンしたソースファイル数: " + $srcFiles.Count)
[void]$report.AppendLine("")
[void]$report.AppendLine("## 全体サマリ")
[void]$report.AppendLine("")
[void]$report.AppendLine("| 区分 | 件数 | 割合 |")
[void]$report.AppendLine("|------|------|------|")
[void]$report.AppendLine(("| **関数定義あり (DONE)** | {0} | {1:P1} |" -f $done.Count, ($done.Count / $total)))
[void]$report.AppendLine(("| ファイル名一致のみ (FILE_ONLY) | {0} | {1:P1} |" -f $file_only.Count, ($file_only.Count / $total)))
[void]$report.AppendLine(("| **未検出 (MISSING)** | {0} | {1:P1} |" -f $missing.Count, ($missing.Count / $total)))
[void]$report.AppendLine(("| 合計 | {0} | 100% |" -f $total))
[void]$report.AppendLine("")

if ($summary.Count -gt 0) {
    [void]$report.AppendLine("## カテゴリ x layer 別の内訳")
    [void]$report.AppendLine("")
    [void]$report.AppendLine("| Category | Layer | DONE | FILE_ONLY | MISSING | Total | DONE 率 |")
    [void]$report.AppendLine("|----------|-------|------|-----------|---------|-------|---------|")
    foreach ($k in ($summary.Keys | Sort-Object)) {
        $parts = $k.Split('|')
        $row = $summary[$k]
        $rate = if ($row.total -gt 0) { ($row.done / $row.total) } else { 0 }
        [void]$report.AppendLine(("| {0} | {1} | {2} | {3} | {4} | {5} | {6:P1} |" -f `
            $parts[0], $parts[1], $row.done, $row.file_only, $row.missing, $row.total, $rate))
    }
    [void]$report.AppendLine("")
}

[void]$report.AppendLine("## MISSING シンボル (関数定義もファイル名一致も検出されず)")
[void]$report.AppendLine("")
if ($missing.Count -eq 0) {
    [void]$report.AppendLine("(なし)")
} else {
    [void]$report.AppendLine("``````")
    foreach ($s in $missing) { [void]$report.AppendLine($s) }
    [void]$report.AppendLine("``````")
}

[void]$report.AppendLine("")
[void]$report.AppendLine("## FILE_ONLY シンボル (ファイルはあるが定義が緩く検出できなかったもの)")
[void]$report.AppendLine("")
if ($file_only.Count -eq 0) {
    [void]$report.AppendLine("(なし)")
} else {
    [void]$report.AppendLine("``````")
    foreach ($s in $file_only) { [void]$report.AppendLine($s) }
    [void]$report.AppendLine("``````")
}

$out = Join-Path $PSScriptRoot "impl_status_report.md"
Set-Content -Path $out -Value $report.ToString() -Encoding UTF8
Write-Host ""
Write-Host ("Report written to: " + $out)
