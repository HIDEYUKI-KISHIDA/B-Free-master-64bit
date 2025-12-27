param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot ".."))
)

Write-Host "Scanning sources under: $Root"

$changelogPath = Join-Path $Root "ChangeLog"
$contrib = @{}
$_examples = @{}
function Add-Contrib([string]$name, [string]$alias, [string]$email, [Nullable[datetime]]$date) {
    if (-not $name -and $alias) { $name = $alias }
    $key = ($name -replace '\\s+', ' ').Trim()
    if (-not $key) { return }
    if (-not $contrib.ContainsKey($key)) {
        $contrib[$key] = [ordered]@{
            Name = $key
            Aliases = New-Object System.Collections.Generic.HashSet[string]
            Emails = New-Object System.Collections.Generic.HashSet[string]
            Count = 0
            FirstSeen = $null
            LastSeen = $null
        }
    }
    if ($alias) { [void]$contrib[$key].Aliases.Add($alias.Trim()) }
    if ($email) { [void]$contrib[$key].Emails.Add($email.Trim()) }
    $contrib[$key].Count++
    if ($date) {
        if (-not $contrib[$key].FirstSeen -or $date -lt $contrib[$key].FirstSeen) { $contrib[$key].FirstSeen = $date }
        if (-not $contrib[$key].LastSeen  -or $date -gt $contrib[$key].LastSeen)  { $contrib[$key].LastSeen  = $date }
    }
}

# Known alias mapping
$aliasMap = @{
    'night'   = 'Naitoh Ryuichi'
    'R.Naitoh' = 'Naitoh Ryuichi'
    'rnaitoh' = 'Naitoh Ryuichi'
    'naitoh'  = 'Naitoh Ryuichi'
    'liu1'    = 'liu1'
}

# Merge external alias map if present (tools/alias_map.json)
try {
    $aliasJsonPath = Join-Path $PSScriptRoot 'alias_map.json'
    if (Test-Path $aliasJsonPath) {
        $json = Get-Content -Raw -Path $aliasJsonPath -Encoding UTF8 | ConvertFrom-Json
        foreach ($k in $json.PSObject.Properties.Name) {
            $v = [string]$json.$k
            if ($k -and $v) { $aliasMap[$k] = $v }
        }
        Write-Host "Merged alias map from: $aliasJsonPath"
    }
} catch {
    Write-Host "Warning: failed to read alias_map.json: $_"
}

# Parse ChangeLog if present
if (Test-Path $changelogPath) {
    $cl = Get-Content -Raw -Path $changelogPath -Encoding UTF8
    $lines = $cl -split "`n"
    foreach ($line in $lines) {
        # Example: Wed Oct 11 01:28:00 1995  Naitoh Ryuichi  (night@ibmpc0.bfree.rim.or.jp)
        if ($line -match '^(?<dow>\\w{3})\\s+(?<mon>\\w{3})\\s+(?<day>\\d{1,2})\\s+(?<time>\\d{2}:\\d{2}:\\d{2})\\s+(?<year>\\d{4})\\s+(?<name>[^\(]+?)\\s*\((?<email>[^\)]+)\)') {
            $dateStr = "$($Matches.mon) $($Matches.day) $($Matches.year) $($Matches.time)"
            $dt = $null
            [void][datetime]::TryParse($dateStr, [ref]$dt)
            $name = ($Matches.name).Trim()
            $email = ($Matches.email).Trim()
            $alias = $null
            if ($email -match '^(?<user>[^@]+)@') { $alias = $Matches.user }
            if ($aliasMap.ContainsKey($alias)) { $name = $aliasMap[$alias] }
            Add-Contrib -name $name -alias $alias -email $email -date $dt
        }
    }
}

# Scan headers ($Author, $Header, Modified by)
$files = Get-ChildItem -Path $Root -Recurse -File -ErrorAction SilentlyContinue |
         Where-Object { $_.Extension -notin '.o','.obj','.exe','.bin','.img','.iso' }

foreach ($f in $files) {
    try {
        $head = Get-Content -Path $f.FullName -TotalCount 60 -ErrorAction Stop -Encoding UTF8
    } catch {
        continue
    }
    foreach ($line in $head) {
        # $Author: liu1 $
        if ($line -match '\\$Author:\\s*(?<author>[^\\$]+)\\$') {
            $alias = ($Matches.author).Trim()
            $name = $alias
            if ($aliasMap.ContainsKey($alias)) { $name = $aliasMap[$alias] }
            Add-Contrib -name $name -alias $alias -email $null -date $null
            # keep up to 3 example files per contributor name
            if (-not $_examples.ContainsKey($name)) { $_examples[$name] = New-Object System.Collections.Generic.HashSet[string] }
            $rel = Resolve-Path $f.FullName | ForEach-Object { $_.Path.Substring($Root.Length).TrimStart('\\','/') } | ForEach-Object { ($_ -replace '\\','/') }
            if ($rel) { [void]$_examples[$name].Add($rel) }
        }
        # $Header: ... v 1.1 2011/12/27 ... liu1 Exp $
        elseif ($line -match '\\$Header: .*? (?<year>\\d{4})[/-](?<mon>\\d{2})[/-](?<day>\\d{2}).*? (?<author>[A-Za-z0-9_.-]+)\\s+Exp \\$') {
            $alias = ($Matches.author).Trim()
            $name = $alias
            if ($aliasMap.ContainsKey($alias)) { $name = $aliasMap[$alias] }
            $dt = Get-Date -Year ([int]$Matches.year) -Month ([int]$Matches.mon) -Day ([int]$Matches.day)
            Add-Contrib -name $name -alias $alias -email $null -date $dt
            if (-not $_examples.ContainsKey($name)) { $_examples[$name] = New-Object System.Collections.Generic.HashSet[string] }
            $rel = Resolve-Path $f.FullName | ForEach-Object { $_.Path.Substring($Root.Length).TrimStart('\\','/') } | ForEach-Object { ($_ -replace '\\','/') }
            if ($rel) { [void]$_examples[$name].Add($rel) }
        }
        # Modified by R.Naitoh (B-Free)
        elseif ($line -match 'Modified by\\s+(?<modby>[^\r\n]+)') {
            $alias = ($Matches.modby).Trim()
            $name = $alias
            if ($aliasMap.ContainsKey($alias)) { $name = $aliasMap[$alias] }
            Add-Contrib -name $name -alias $alias -email $null -date $null
            if (-not $_examples.ContainsKey($name)) { $_examples[$name] = New-Object System.Collections.Generic.HashSet[string] }
            $rel = Resolve-Path $f.FullName | ForEach-Object { $_.Path.Substring($Root.Length).TrimStart('\\','/') } | ForEach-Object { ($_ -replace '\\','/') }
            if ($rel) { [void]$_examples[$name].Add($rel) }
        }
    }
}

# Scan doc emails (acknowledgements)
$docFiles = Get-ChildItem -Path (Join-Path $Root 'doc') -Recurse -File -ErrorAction SilentlyContinue
foreach ($df in $docFiles) {
    try {
        $txt = Get-Content -Path $df.FullName -ErrorAction Stop -Encoding UTF8
    } catch { continue }
    foreach ($line in $txt) {
        if ($line -match '([A-Za-z0-9._%+-]+)@([A-Za-z0-9.-]+)') {
            $email = $Matches[0]
            $alias = $Matches[1]
            $name = $alias
            if ($aliasMap.ContainsKey($alias)) { $name = $aliasMap[$alias] }
            Add-Contrib -name $name -alias $alias -email $email -date $null
        }
    }
}

# Emit CONTRIBUTORS.md
$outPath = Join-Path $Root 'CONTRIBUTORS.md'
$items = $contrib.GetEnumerator() | Sort-Object { -$_.Value.Count }, { $_.Value.Name }

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('# Contributors (PC9801 subtree)')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('Auto-generated from ChangeLog and RCS headers ($Author/$Header).')
[void]$sb.AppendLine('')

foreach ($e in $items) {
    $v = $e.Value
    $aliases = ''
    if ($v.Aliases.Count -gt 0) { $aliases = '(' + ([string]::Join(', ', $v.Aliases)) + ')' }
    $emails = ''
    if ($v.Emails.Count -gt 0) { $emails = ' emails: ' + ([string]::Join(', ', $v.Emails)) }
    $span = ''
    if ($v.FirstSeen -and $v.LastSeen) { $span = ' [' + $v.FirstSeen.ToString('yyyy') + '-' + $v.LastSeen.ToString('yyyy') + ']' }
    elseif ($v.FirstSeen) { $span = ' [since ' + $v.FirstSeen.ToString('yyyy') + ']' }
    $line = '- ' + $v.Name + ' ' + $aliases + $span + ' - commits: ' + $v.Count + $emails
    [void]$sb.AppendLine($line)
    if ($_examples.ContainsKey($v.Name) -and $_examples[$v.Name].Count -gt 0) {
        $list = @($_examples[$v.Name]) | Select-Object -First 3
        [void]$sb.AppendLine('  e.g., ' + ([string]::Join(', ', $list)))
    }
}

[void]$sb.AppendLine('')
[void]$sb.AppendLine('Sources: ChangeLog, file headers: $Author, $Header, "Modified by".')

$md = $sb.ToString()
Set-Content -Path $outPath -Value $md -Encoding UTF8
Write-Host "Wrote: $outPath"
