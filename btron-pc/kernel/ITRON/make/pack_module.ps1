param(
    [string]$Image = "itron.image",
    [string]$Output = "itron.module",
    [string]$Symbol = "startup"
)

$sections = @(".text", ".data", ".rdata", ".eh_fram", ".idata", ".reloc")
$tools = "C:\msys64\usr\bin"
$objcopy = Join-Path $tools "objcopy.exe"
$objdump = Join-Path $tools "objdump.exe"
$nm = Join-Path $tools "nm.exe"

if (-not (Test-Path $objcopy)) {
    throw "objcopy not found at $objcopy"
}
if (-not (Test-Path $objdump)) {
    throw "objdump not found at $objdump"
}
if (-not (Test-Path $nm)) {
    throw "nm not found at $nm"
}
if (-not (Test-Path $Image)) {
    throw "input image '$Image' not found"
}

$payload = [System.IO.Path]::ChangeExtension($Output, ".payload")
$copyArgs = @("-O", "binary", "--gap-fill=0")
foreach ($sec in $sections) {
    $copyArgs += @("-j", $sec)
}
$copyArgs += @($Image, $payload)
& $objcopy @copyArgs

try {
    $objdumpOut = & $objdump "-h" $Image
} catch {
    Remove-Item -ErrorAction SilentlyContinue $payload
    throw
}
$match = [regex]::Match($objdumpOut, "\.bss\s+([0-9a-fA-F]+)")
if (-not $match.Success) {
    Remove-Item -ErrorAction SilentlyContinue $payload
    throw "Failed to parse .bss size from objdump output"
}
$BssSize = [Convert]::ToInt32($match.Groups[1].Value, 16)

try {
    $nmOut = & $nm $Image
} catch {
    Remove-Item -ErrorAction SilentlyContinue $payload
    throw
}
$escapedSymbol = [regex]::Escape($Symbol)
$symbolPattern = "(?<addr>[0-9a-fA-F]+)\s+T\s+$escapedSymbol"
$entryMatch = [regex]::Match($nmOut, $symbolPattern)
if (-not $entryMatch.Success) {
    Remove-Item -ErrorAction SilentlyContinue $payload
    throw "Failed to locate symbol '$Symbol' in nm output"
}
$Entry = [Convert]::ToInt32($entryMatch.Groups['addr'].Value, 16)

$payloadBytes = [System.IO.File]::ReadAllBytes($payload)
$header = New-Object byte[] 512
$fields = @(
    (267 -band 0xFFFF) -bor ((100 -band 0xFF) -shl 16),
    $payloadBytes.Length,
    0,
    $BssSize,
    0,
    $Entry,
    0,
    0
)
$offset = 0
foreach ($value in $fields) {
    $bytes = [System.BitConverter]::GetBytes([uint32]$value)
    $bytes.CopyTo($header, $offset)
    $offset += 4
}

$moduleBytes = New-Object System.Collections.Generic.List[byte]
$moduleBytes.AddRange($header)
$moduleBytes.AddRange($payloadBytes)
[System.IO.File]::WriteAllBytes($Output, $moduleBytes.ToArray())
Remove-Item -ErrorAction SilentlyContinue $payload

Write-Output ("Created module {0} (text+data={1} bytes, bss={2} bytes, entry=0x{3:X})" -f $Output, $payloadBytes.Length, $BssSize, $Entry)
