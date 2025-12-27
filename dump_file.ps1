param([string]$Path)
if (-not $Path) { Write-Error 'Usage: .\dump_file.ps1 <path>'; exit 1 }
if (-not (Test-Path $Path)) { Write-Output 'MISSING'; exit 0 }
$b = [System.IO.File]::ReadAllBytes($Path)
$len = [Math]::Min($b.Length,512)
for ($i=0; $i -lt $len; $i+=16) {
  $line=@()
  for ($j=0; ($j -lt 16) -and (($i+$j) -lt $len); $j++) { $line += $b[$i+$j].ToString('X2') }
  Write-Output ('{0:X4}: {1}' -f $i, ($line -join ' '))
}