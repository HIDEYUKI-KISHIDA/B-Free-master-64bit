$path = 'C:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd\bootdisk.img'
$b = [System.IO.File]::ReadAllBytes($path)
$len = [Math]::Min($b.Length,1024)
for ($i=0; $i -lt $len; $i+=16) {
    $line = @()
    for ($j=0; ($j -lt 16) -and (($i+$j) -lt $len); $j++) {
        $line += $b[$i+$j].ToString('X2')
    }
    Write-Output ('{0:X4}: {1}' -f $i, ($line -join ' '))
}