$missing = @('_longjmp','_tolower','_toupper','endnetent','fpclassify',
             'isgreater','isgreaterequal','isless','islessequal','islessgreater',
             'isnormal','isunordered')
$root = 'C:\Users\h_kis\Desktop\B-Free-master\Program\bfree_x86_64'
$files = Get-ChildItem -Path $root -Recurse -Include *.c,*.h,*.S,*.s -File -ErrorAction SilentlyContinue
foreach ($s in $missing) {
    Write-Host "--- $s ---" -ForegroundColor Cyan
    $hits = $files | Select-String -SimpleMatch -Pattern $s -List -ErrorAction SilentlyContinue | Select-Object -First 6
    foreach ($h in $hits) {
        $rel = $h.Path.Substring($root.Length + 1)
        Write-Host "  $rel : line $($h.LineNumber)"
    }
    if (-not $hits) { Write-Host "  (no hits anywhere)" -ForegroundColor Yellow }
}
