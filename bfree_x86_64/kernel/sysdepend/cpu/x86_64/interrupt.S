# 各エントリをCから参照できるよう型宣言も追加
.type knl_interrupt_entry0, @function
.type knl_interrupt_entry1, @function
.type knl_interrupt_entry2, @function
.type knl_interrupt_entry3, @function
.type knl_interrupt_entry4, @function
.type knl_interrupt_entry5, @function
.type knl_interrupt_entry6, @function
.type knl_interrupt_entry7, @function
.type knl_interrupt_entry8, @function
.type knl_interrupt_entry9, @function
.type knl_interrupt_entry10, @function
.type knl_interrupt_entry11, @function
.type knl_interrupt_entry12, @function
.type knl_interrupt_entry13, @function
.type knl_interrupt_entry14, @function
.type knl_interrupt_entry15, @function
.type knl_interrupt_entry16, @function
.type knl_interrupt_entry17, @function
.type knl_interrupt_entry18, @function
.type knl_interrupt_entry19, @function
.type knl_interrupt_entry20, @function
.type knl_interrupt_entry21, @function
.type knl_interrupt_entry22, @function
.type knl_interrupt_entry23, @function
.type knl_interrupt_entry24, @function
.type knl_interrupt_entry25, @function

# --- 割り込みスタブ自動生成・配列化 ---

.macro SAVE_ALL
    pushq %rbp
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax
.endm

.macro RESTORE_ALL
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    popq %rbp
.endm

.section .text
.align 16


# --- 割り込みスタブ自動生成（GAS .macro + .irp 方式） ---

.macro INTERRUPT_STUB num
    .align 16
    .globl knl_interrupt_stub_\num
    .type knl_interrupt_stub_\num, @function
knl_interrupt_stub_\num:
    /* エラーコードを積む例外ベクタ */
    .if (\num == 8 || (\num >= 10 && \num <= 14) || \num == 17 || \num == 21 || \num == 29 || \num == 30)
        /* CPUが自動でErrorCodeを積むので何もしない */
    .else
        pushq $0
    .endif
    SAVE_ALL
    /* --- 16バイトアライメント調整 --- */
    movq %rsp, %rbx
    andq $-16, %rsp
    pushq %rbx

    subq $8, %rsp
    movq %rsp, %rdi
    addq $8, %rdi
    movq $\num, %rsi
    call knl_interrupt_main
    addq $8, %rsp

    popq %rsp  /* アライメント復元 */

    RESTORE_ALL
    addq $8, %rsp
    iretq
.endm

# 256個のスタブ生成
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
    INTERRUPT_STUB \n
.endr

# --- C言語から参照するための配列定義 ---
.section .data
.align 8
.global knl_interrupt_stubs
knl_interrupt_stubs:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
    .quad knl_interrupt_stub_\n
.endr

.section .note.GNU-stack,"",@progbits
