# x86_64 timer interrupt entry (all registers push, call C handler)

.global timer_interrupt_entry
.extern timer_interrupt_handler_c

timer_interrupt_entry:
    # Push general-purpose registers (reverse order)
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax

    # Push dummy err_code and int_no (IRQ0=32)
    pushq $0
    pushq $32

    # RIP, CS, RFLAGS, RSP, SS are pushed by CPU
    # RSP points to regs_x86_64_t
    mov %rsp, %rdi
    call timer_interrupt_handler_c

    # Pop dummy int_no and err_code
    add $16, %rsp
    # Pop registers (reverse order)
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rbp
    popq %rdi
    popq %rsi
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    iretq

.section .note.GNU-stack,"",@progbits
