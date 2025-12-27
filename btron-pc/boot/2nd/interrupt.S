
.globl int80_handler
int80_handler:
    push %eax
    push %ecx
    push %edx
    push %ebx
    push %ebp
    push %esi
    push %edi
    movl %esp, %eax
    push %eax
    call syscall_dispatch
    addl $4, %esp
    pop %edi
    pop %esi
    pop %ebp
    pop %ebx
    pop %edx
    pop %ecx
    pop %eax
    iret
