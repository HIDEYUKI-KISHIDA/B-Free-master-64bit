ENTRY(_start)

SECTIONS
{
  /* 1MB（0x100000）から開始 */
  . = 0x100000;


  /* AT(0)を削除し、物理アドレスも0x100000にする */

  .multiboot2 : {
    KEEP(*(.multiboot2))
  }

  .note.gh.pvh : {
    KEEP(*(.note.gh.pvh))
  }

  /* PVH Noteセクションを削除しない */
  .note.gnu.pvh : {
    KEEP(*(.note.gnu.pvh))
  }

  /* その他のセクションも1MB以降に配置 */
  . = ALIGN(4K);
  /* 0x101000 (1MB + 4KB) から開始 */
  . = ALIGN(4K);
  .text : {
    KEEP(*(.text._start))
    *(.text.boot)
    *(.text .text.*)
  }

  .rodata : ALIGN(4K) {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(4K) {
    *(.data .data.*)
  }

  .bss : ALIGN(4K) {
    _bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    _bss_end = .;
  }

  /DISCARD/ : { *(.note.GNU-stack) }
}
}