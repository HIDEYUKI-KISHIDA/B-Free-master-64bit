TK2_x86_64_Spec.md
cat << 'EOF' > TK2_x86_64_Spec_v2.3.md
# T-Kernel 2.0 x86_64 Porting Specification (v2.3 Final)
## Project: Bfree (BTRON) 64-bit Migration

**Document ID**: TK2-PORT-x86_64-005
**Date**: 2026-03-31
**Target**: T-Kernel 2.0 → x86_64 (Long Mode)

---

## 1. Global Context for Copilot
**Important**: Paste this block at the beginning of every prompt to Copilot to ensure architectural consistency.

* **Mode**: x86_64 Long Mode (64-bit).
* **ABI**: System V AMD64 ABI (Args: RDI, RSI, RDX, RCX, R8, R9).
* **Memory Layout**: Physical 0x100000 (1MB), Identity-mapped.
* **Compiler Flags**: `-mcmodel=large`, `-mno-red-zone`, `-ffreestanding`, `-nostdlib`.
* **Stack Alignment**: RSP must be 16-byte aligned BEFORE any `call` instruction.
* **Context Switch**: "Design A" (Dispatching is performed inside `interrupt.S` immediately after the C handler returns).
* **Struct Matching**: `T_REGS` members must be defined in the EXACT REVERSE order of the `pushq` sequence.

---

## 2. Implementation Details by File

### 2.1 Build Environment
**File**: `${TK_SRC}/sysdepend/x86_64/makefile.common`

* **Mandatory CFLAGS**: `-m64`, `-mno-red-zone`, `-mcmodel=large`, `-fno-stack-protector`, `-nostdlib`, `-ffreestanding`.
* **Linker Settings**: Use `x86_64-elf-ld` with `-melf_x86_64` and link with the custom `.lnk` script.

### 2.2 Boot Sequence & Paging
**File**: `${TK_SRC}/kernel/sysmain/sysdepend/x86_64/reset.S`

* **Page Table Placement**: `boot_pml4`, `boot_pdpt`, and `boot_pd` MUST be defined in the **`.data` section**.
    * *Constraint*: Placing tables in `.bss` will result in their destruction during the BSS clearing process.
* **Transition Flow**:
    1.  Disable interrupts (`cli`).
    2.  Load initial GDT (`lgdt`) containing the 64-bit code segment.
    3.  Set `CR3` to the physical address of `boot_pml4`.
    4.  Enable PAE (`CR4.bit5 = 1`) and Long Mode (`EFER.LME = 1`).
    5.  Enable Paging (`CR0.PG = 1`) to transition to Long Mode.
    6.  Execute `ljmp $0x08, $long_mode_entry` to update `CS` and enter full 64-bit mode.
    7.  Clear the BSS section using `rep stosb` (based on `_bss_start` and `_bss_end`).
    8.  Initialize `RSP` to `knl_kernel_stack_top` and execute `call knl_main`.

### 2.3 Register Save Structure (T_REGS)
**File**: `${TK_SRC}/include/tk/sysdef_depend.h`

* **Definition**: Members must align with the stack layout created by the `pushq` sequence in `interrupt.S`.

```c
typedef struct {
    /* Low Address: Last pushed by 'pushq %rax' */
    uint64_t rax, rbx, rcx, rdx, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rbp;
    /* Hardware Frame: Automatically pushed by CPU during interrupt */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} T_REGS; /* Total size: 160 bytes (16-byte aligned) */