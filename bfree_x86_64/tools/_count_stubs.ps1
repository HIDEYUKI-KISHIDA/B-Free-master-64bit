$root = 'C:\Users\h_kis\Desktop\B-Free-master\Program\bfree_x86_64\userland\libc'
$files = Get-ChildItem $root -Filter *.c -Recurse -File
$stubFiles = New-Object System.Collections.ArrayList
foreach ($f in $files) {
    try { $t = [IO.File]::ReadAllText($f.FullName) } catch { continue }
    if (($t -match 'ENOSYS') -and ($t -match 'return\s*-1')) {
        [void]$stubFiles.Add($f.FullName.Substring($root.Length + 1))
    }
}
Write-Host "Files containing ENOSYS+return -1 stubs: $($stubFiles.Count)"
$stubFiles | Sort-Object | ForEach-Object { Write-Host "  $_" }
