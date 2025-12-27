$p = 'C:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd\2ndboot64'
$b = [System.IO.File]::ReadAllBytes($p)
$len = [Math]::Min($b.Length,256)
for ($i=0; $i -lt $len; $i+=16) {
  $line=@()
  for ($j=0; ($j -lt 16) -and (($i+$j) -lt $len); $j++) { $line += $b[$i+$j].ToString('X2') }
  Write-Output ('{0:X4}: {1}' -f $i, ($line -join ' '))
}