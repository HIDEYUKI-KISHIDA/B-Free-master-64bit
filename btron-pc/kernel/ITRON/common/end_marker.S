/*
 * Reserve a concrete end symbol that survives the PE linker.
 */

    .section .bss
    .align 16
    .globl end
    .globl _end
    .globl __end__
end:
_end:
__end__:
