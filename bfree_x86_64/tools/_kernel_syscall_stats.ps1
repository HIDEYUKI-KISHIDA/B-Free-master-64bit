$f = 'C:\Users\h_kis\Desktop\B-Free-master\Program\bfree_x86_64\kernel\sysmain\syscall.c'
$lines = Get-Content $f
Write-Host "syscall.c total lines: $($lines.Count)"

$caseHits = Select-String -Path $f -Pattern '^\s*case\s+' -AllMatches
Write-Host "case statements: $($caseHits.Count)"

$handlers = Select-String -Path $f -Pattern '^\s*(static\s+)?(long|int|uint64_t|void)\s+(bfree_sys|knl_sys|sys_)' -AllMatches
Write-Host "handler-like functions: $($handlers.Count)"

# bfree/kernel/syscall.c もある場合
$f2 = 'C:\Users\h_kis\Desktop\B-Free-master\Program\bfree_x86_64\bfree\kernel\syscall.c'
if (Test-Path $f2) {
    $lines2 = Get-Content $f2
    Write-Host ""
    Write-Host "bfree/kernel/syscall.c total lines: $($lines2.Count)"
    $caseHits2 = Select-String -Path $f2 -Pattern '^\s*case\s+' -AllMatches
    Write-Host "case statements: $($caseHits2.Count)"
}

# syscall 番号定義を含むヘッダを探す
$includes = Get-ChildItem 'C:\Users\h_kis\Desktop\B-Free-master\Program\bfree_x86_64\kernel\include' -Recurse -Include *.h -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "kernel/include headers: $($includes.Count)"

# extern 宣言された syscall サブ関数の数
$sub = Select-String -Path $f -Pattern '^\s*extern\s+' -AllMatches
Write-Host "extern decls in syscall.c: $($sub.Count)"
