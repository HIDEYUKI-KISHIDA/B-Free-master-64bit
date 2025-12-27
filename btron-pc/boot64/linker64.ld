/* linker64.ld - 64ビットカーネル用リンカスクリプト雛形 */
ENTRY(kernel_main)
SECTIONS {
    . = 1M;
    .text : { *(.text) }
    .data : { *(.data) }
    .bss  : { *(.bss)  }
}
