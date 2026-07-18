# 64bit IDTロード関数（Cから呼び出し用）
    .section .text
    .globl init_idt
init_idt:
    lea idt_ptr(%rip), %rax
    lidt (%rax)
    ret
###############################################################
# reset.S - x86_64 T-Kernel2.0 ブート・初期化シーケンス（32bit→64bit遷移対応）
###############################################################

#---------------------------------------------------
# 1. Multiboot2 Header (8byteアライン & 先頭8KB以内)
#---------------------------------------------------
.section .multiboot2, "a"
.align 8
multiboot_header_start:
    .long 0xe85250d6                # Magic number
    .long 0                         # Architecture (0: i386)
    .long multiboot_header_end - multiboot_header_start
    .long -(0xe85250d6 + 0 + (multiboot_header_end - multiboot_header_start))
    # Framebuffer request tag: GRUB に 1024x768x32 VBE 画面モードを要求
    .align 8
    .short 5      # MULTIBOOT_HEADER_TAG_FRAMEBUFFER
    .short 0      # flags (required)
    .long 20      # tag size
    .long 1024    # width
    .long 768     # height
    .long 32      # depth (bpp)
    # 終了タグ
    .align 8
    .short 0
    .short 0
    .long 8
multiboot_header_end:


#---------------------------------------------------
# 2. 32bit Entry Point
#---------------------------------------------------
    .section .data
    .align 8
    .globl idt_ptr
idt_ptr:
    .word 256*16-1     # limit
    .quad idt          # base

    .align 4
    .globl mb2_magic_storage
mb2_magic_storage: .long 0
    .globl mb2_info_storage
mb2_info_storage:  .long 0
.section .text
.code32
.global _start
_start:
    cli
    # 【追加】Multiboot2の情報をメモリへ退避
    movl %eax, mb2_magic_storage
    movl %ebx, mb2_info_storage

    # 32bit用の一時スタック設定（既存の領域を再利用）
    movl $knl_kernel_stack_top, %esp

    # ページテーブル(PML4)のアドレスをCR3にセット
    movl $boot_pml4, %eax
    movl %eax, %cr3

    # CR4.PAE (Physical Address Extension) を有効化 (Bit 5)
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    # EFER MSR (0xC0000080) の LME (Long Mode Enable) ビットをセット
    movl $0xc0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    # CR0.PG (Paging) を有効化して Long Mode を確定させる
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    # 64bit GDTをロード
    lgdt gdt64_desc

    # 64bitコードセグメント(0x08)へ遠距離ジャンプ (Long Jump)
    # これによりCPUの内部モードが完全に64bitに切り替わる
    ljmp $0x08, $long_mode_entry


#---------------------------------------------------
# 3. 64bit Long Mode
#---------------------------------------------------
.code64
long_mode_entry:
    # データセグメントレジスタの初期化
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    # 64bitスタックポインタの再設定
    lea knl_kernel_stack_top(%rip), %rsp

    # 【修正】保存しておいた値をメモリから復元して引数セット
    movl mb2_magic_storage(%rip), %edi
    movl mb2_info_storage(%rip), %esi
    .extern bfree_entry
    call bfree_entry

    # 戻ってきたら停止
1:  hlt
    jmp 1b

#---------------------------------------------------
# 4. データ定義 (既存のものを流用)
#---------------------------------------------------
.section .data
.align 4096
boot_pml4:
    .quad boot_pdpt + 0x03
    .fill 511, 8, 0
boot_pdpt:
    .quad boot_pd + 0x03           # PDPT[0]: 0-1GB identity map
    .quad 0                        # PDPT[1]: 1-2GB unmapped
    .quad 0                        # PDPT[2]: 2-3GB unmapped
    .quad boot_pd_mmio + 0x03      # PDPT[3]: 3-4GB → MMIO (VGA VRAM at 0xE0000000)
    .fill 508, 8, 0
boot_pd:
    .rept 8   # 2MB × 8 = 16MB分をidentity map
        .quad (((. - boot_pd) >> 3) << 21) + 0x83
    .endr

# 3-4GB範囲の MMIO ページディレクトリ
# QEMU stdvga VRAM: 0xE0000000 (BAR0 = 3.5GB)
# PDPT[3]の 3GB基点からのオフセット: 0xE0000000 - 0xC0000000 = 0x20000000 = 512MB
# PD index = 512MB / 2MB = 256
# QEMU virtio-vga VRAM: 0xFD000000 (PD index = 488)
.align 4096
boot_pd_mmio:
    .fill 256, 8, 0                # PD[0-255]: 3GB-3.5GB unmapped
    .quad 0xE0000000 | 0x83        # PD[256]: 0xE0000000 2MB (Present|RW|PS)
    .quad 0xE0200000 | 0x83        # PD[257]: 0xE0200000 2MB
    .quad 0xE0400000 | 0x83        # PD[258]: 0xE0400000 2MB
    .quad 0xE0600000 | 0x83        # PD[259]: 0xE0600000 2MB
    .quad 0xE0800000 | 0x83        # PD[260]: 0xE0800000 2MB
    .quad 0xE0A00000 | 0x83        # PD[261]: 0xE0A00000 2MB
    .quad 0xE0C00000 | 0x83        # PD[262]: 0xE0C00000 2MB
    .quad 0xE0E00000 | 0x83        # PD[263]: 0xE0E00000 2MB (16MB計)
    .fill 224, 8, 0                # PD[264-487]: unmapped
    .quad 0xFD000000 | 0x83        # PD[488]: 0xFD000000 2MB (QEMU virtio-vga VRAM)
    .quad 0xFD200000 | 0x83        # PD[489]: 0xFD200000 2MB
    .quad 0xFD400000 | 0x83        # PD[490]: 0xFD400000 2MB
    .quad 0xFD600000 | 0x83        # PD[491]: 0xFD600000 2MB (8MB計)
    .fill 20, 8, 0                 # PD[492-511]: unmapped

.align 8
gdt64:
    .quad 0x0000000000000000    # Null
    .quad 0x00af9a000000ffff    # 0x08: Code
    .quad 0x00cf92000000ffff    # 0x10: Data
    .quad 0x00affa000000ffff    # 0x18: User Code (ring3)
    .quad 0x00cff2000000ffff    # 0x20: User Data (ring3)
gdt64_desc:
    .word (5 * 8) - 1
    .quad gdt64

#----------------------
# BSS領域シンボル
.section .bss
.global _bss_start, _bss_end
_bss_start:
    .skip 1
_bss_end:
    .skip 1

#----------------------
# スタック専用セクション（BSSと分離）
.section .stack, "aw", @nobits
.align 16
stack_bottom:
    .skip 16384
.global knl_kernel_stack_top
knl_kernel_stack_top:

.section .note.GNU-stack,"",@progbits