# B-Free OS 64-bit ISO Image Creator (Windows PowerShell)
# Creates a bootable ISO image for QEMU/VirtualBox

param(
    [string]$BootImage = "2ndboot64",
    [string]$ISOOutput = "bfree-64bit.iso"
)

# Check if boot image exists
if (-not (Test-Path $BootImage)) {
    Write-Error "Error: $BootImage not found. Build it first with 'make 2ndboot64'"
    exit 1
}

# Create temporary directory
$TempDir = Join-Path $env:TEMP "bfree_iso_$([System.Guid]::NewGuid().ToString().Substring(0,8))"
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null
New-Item -ItemType Directory -Path "$TempDir\boot" -Force | Out-Null
New-Item -ItemType Directory -Path "$TempDir\boot\grub" -Force | Out-Null

try {
    # Copy boot image
    Copy-Item $BootImage "$TempDir\boot\bfree.bin" -Force
    
    # Create GRUB configuration
    $grubCfg = @"
set timeout=5
set default=0

menuentry 'B-Free OS 64-bit' {
    multiboot /boot/bfree.bin
    boot
}

menuentry 'B-Free OS 64-bit (Debug)' {
    multiboot /boot/bfree.bin console=com1
    boot
}
"@
    
    Set-Content -Path "$TempDir\boot\grub\grub.cfg" -Value $grubCfg -Encoding ASCII
    
    # Try to create ISO using available tools
    $isoCreated = $false
    
    # Try mkisofs
    if ((Get-Command mkisofs -ErrorAction SilentlyContinue) -ne $null) {
        Write-Host "Creating ISO with mkisofs..."
        & mkisofs -R -b boot/grub/stage2_eltorito `
                  -no-emul-boot -boot-load-size 4 `
                  -boot-info-table -o $ISOOutput $TempDir
        $isoCreated = $true
    }
    # Try xorriso
    elseif ((Get-Command xorriso -ErrorAction SilentlyContinue) -ne $null) {
        Write-Host "Creating ISO with xorriso..."
        & xorriso -as mkisofs -R -o $ISOOutput $TempDir
        $isoCreated = $true
    }
    # Try genisoimage
    elseif ((Get-Command genisoimage -ErrorAction SilentlyContinue) -ne $null) {
        Write-Host "Creating ISO with genisoimage..."
        & genisoimage -R -b boot/grub/stage2_eltorito `
                      -no-emul-boot -boot-load-size 4 `
                      -boot-info-table -o $ISOOutput $TempDir
        $isoCreated = $true
    }
    
    if ($isoCreated) {
        Write-Host "ISO image created: $ISOOutput" -ForegroundColor Green
        Write-Host ""
        Write-Host "To run with QEMU:"
        Write-Host "  qemu-system-x86_64 -cdrom $ISOOutput"
        Write-Host ""
        Write-Host "To run with VirtualBox:"
        Write-Host "  1. Create a new VM"
        Write-Host "  2. Set CD/DVD to $ISOOutput"
        Write-Host "  3. Boot from CD/DVD"
    } else {
        Write-Error "Error: No ISO creation tool found"
        Write-Host "Please install one of the following:"
        Write-Host "  - mkisofs (part of cdrtools)"
        Write-Host "  - xorriso"
        Write-Host "  - genisoimage"
        exit 1
    }
}
finally {
    # Clean up temporary directory
    if (Test-Path $TempDir) {
        Remove-Item -Path $TempDir -Recurse -Force
    }
}
