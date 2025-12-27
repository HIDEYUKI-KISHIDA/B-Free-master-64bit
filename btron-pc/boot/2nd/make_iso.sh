#!/bin/bash

# B-Free OS 64-bit ISO Image Creator
# Creates a bootable ISO image for QEMU/VirtualBox

set -e

BOOT_IMAGE="2ndboot64"
ISO_OUTPUT="bfree-64bit.iso"
TEMP_DIR="/tmp/bfree_iso_$$"
KERNEL_ADDR="0x100000"

# Check if boot image exists
if [ ! -f "$BOOT_IMAGE" ]; then
    echo "Error: $BOOT_IMAGE not found. Build it first with 'make 2ndboot64'"
    exit 1
fi

# Create temporary directory
mkdir -p "$TEMP_DIR"
mkdir -p "$TEMP_DIR/boot"
mkdir -p "$TEMP_DIR/boot/grub"

# Copy boot image
cp "$BOOT_IMAGE" "$TEMP_DIR/boot/bfree.bin"

# Create GRUB configuration
cat > "$TEMP_DIR/boot/grub/grub.cfg" << 'EOF'
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
EOF

# Create ISO using mkisofs (if available)
if command -v mkisofs &> /dev/null; then
    echo "Creating ISO with mkisofs..."
    mkisofs -R -b boot/grub/stage2_eltorito \
            -no-emul-boot -boot-load-size 4 \
            -boot-info-table -o "$ISO_OUTPUT" "$TEMP_DIR"
elif command -v xorriso &> /dev/null; then
    echo "Creating ISO with xorriso..."
    xorriso -as mkisofs -R -o "$ISO_OUTPUT" "$TEMP_DIR"
elif command -v genisoimage &> /dev/null; then
    echo "Creating ISO with genisoimage..."
    genisoimage -R -b boot/grub/stage2_eltorito \
                -no-emul-boot -boot-load-size 4 \
                -boot-info-table -o "$ISO_OUTPUT" "$TEMP_DIR"
else
    echo "Error: No ISO creation tool found"
    echo "Please install mkisofs, xorriso, or genisoimage"
    rm -rf "$TEMP_DIR"
    exit 1
fi

# Clean up
rm -rf "$TEMP_DIR"

echo "ISO image created: $ISO_OUTPUT"
echo ""
echo "To run with QEMU:"
echo "  qemu-system-x86_64 -cdrom $ISO_OUTPUT"
echo ""
echo "To run with VirtualBox:"
echo "  1. Create a new VM"
echo "  2. Set CD/DVD to $ISO_OUTPUT"
echo "  3. Boot from CD/DVD"
