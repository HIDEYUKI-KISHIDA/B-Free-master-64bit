# B-Free OS 64-bit Support Implementation Guide

## Overview

This document describes the 64-bit enhancements made to the B-Free OS bootloader and kernel.

## Architecture Changes

### 1. Type System (types.h)
- Added 64-bit type definitions:
  - `WORD64`, `UWORD64` - 64-bit integers
  - `LONG64`, `ULONG64` - 64-bit long integers
  - `L64PTR`, `UL64PTR` - 64-bit pointers

### 2. Boot Process

#### Stage 1: 16-bit Real Mode (start16.S)
- Real mode initialization
- BIOS interrupt calls for memory detection
- Transition to protected mode

#### Stage 2: 32-bit Protected Mode (start32.S)
- Existing 32-bit boot loader
- Can detect CPU support for 64-bit mode
- Option to transition to 64-bit long mode

#### Stage 3: 64-bit Long Mode (start64.S) - NEW
- 64-bit mode initialization
- Setup for 4-level paging (PML4 -> PDPT -> PD -> PT)
- Support for large memory addresses (up to 512GB initially)
- Hardware features:
  - PAE (Physical Address Extension)
  - Long Mode (AMD64)
  - NXE (No-Execute Enforcement)

### 3. Memory Management

#### 32-bit Memory (memory.c)
- Original implementation
- Supports up to 4GB (with PAE)

#### 64-bit Memory (memory64.c) - NEW
- 64-bit malloc/free implementation
- Support for large heaps
- Data Execution Prevention (NXE) support
- Memory region detection from BIOS

### 4. Paging Structure

#### 32-bit Paging (page.h/page.c)
- 2-level page tables
- 32-bit page table entries

#### 64-bit Paging (page.h/page64.c) - NEW
- 4-level page table hierarchy:
  1. PML4 (Page Map Level 4) - 512 entries
  2. PDPT (Page Directory Pointer Table) - 512 entries each
  3. PD (Page Directory) - 512 entries each
  4. PT (Page Table) - 512 entries each
- 64-bit page table entries with extended attributes
- Support for 2MB large pages in page directory

### 5. GDT and IDT

#### 32-bit GDT/IDT (gdt_idt_64.h/gdt_idt_64.c) - NEW
For 64-bit mode:

**GDT Entries:**
- Null descriptor
- 64-bit code segment (selector 0x08)
- 64-bit data segment (selector 0x10)
- Task State Segment (TSS) - 128-bit descriptor

**TSS Features:**
- Ring 0 stack pointer (RSP0)
- Interrupt Stack Table (IST) - 7 stack pointers
- IO permission bitmap

**IDT:**
- 256 interrupt/trap gates
- 64-bit offset
- IST support for exception handling

## Building 64-bit Version

### Makefile Targets

```bash
# Build 32-bit bootloader (default)
make 2ndboot32

# Build 64-bit bootloader
make 2ndboot64

# Clean all
make clean
```

### Compiler Flags

**32-bit:**
```
-c -msoft-float -fno-builtin -Wall -D__LINUX__
```

**64-bit:**
```
-c -msoft-float -fno-builtin -Wall -D__LINUX__ -D__x86_64__ -fPIC
```

## Key Improvements

1. **Larger Address Space**: From 4GB (32-bit) to 512GB+ (64-bit)
2. **More Registers**: 16 general-purpose registers (vs 8 in 32-bit)
3. **Improved Performance**: 64-bit operations, better register utilization
4. **Security Features**: NXE (No-Execute) bit for DEP
5. **Better Memory Management**: Larger heap, better allocation strategies

## File Structure

```
boot/2nd/
├── start16.S          # 16-bit boot code (unchanged)
├── start32.S          # 32-bit boot code (unchanged)
├── start64.S          # 64-bit boot code (NEW)
├── types.h            # 64-bit type definitions (UPDATED)
├── location.h         # Memory layout definitions (UPDATED)
├── page.h             # Paging structure definitions (UPDATED)
├── page.c             # 32-bit paging (unchanged)
├── page64.c           # 64-bit paging (NEW)
├── memory.c           # 32-bit memory management (unchanged)
├── memory64.h         # 64-bit memory management header (NEW)
├── memory64.c         # 64-bit memory management (NEW)
├── gdt_idt_64.h       # 64-bit GDT/IDT definitions (NEW)
├── gdt_idt_64.c       # 64-bit GDT/IDT setup (NEW)
├── main64.c           # 64-bit kernel entry point (NEW)
└── Makefile           # Build configuration (UPDATED)
```

## Known Limitations

1. Currently uses identity mapping for first 512GB
2. Exception handlers are minimal stubs
3. Device driver support needs to be added
4. UEFI support not yet implemented

## Future Enhancements

1. Full kernel port to 64-bit
2. UEFI boot support
3. Advanced memory management (NUMA support)
4. Full exception handling
5. Multi-core support
6. Long-mode specific optimizations

## Testing

To test the 64-bit bootloader:

```bash
# Build 64-bit version
make clean
make 2ndboot64

# Use with qemu-system-x86_64
qemu-system-x86_64 -fda 2ndboot64
```

## References

- AMD64 Architecture Programmer's Manual
- Intel 64 and IA-32 Architectures Software Developer's Manual
- OS Development wikis and references
