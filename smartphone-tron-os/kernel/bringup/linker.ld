OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
	. = STOS_LINK_BASE;

	.text.head : {
		KEEP(*(.text.head))
	}

	.text : {
		*(.text .text.*)
	}

	.rodata : {
		*(.rodata .rodata.*)
	}

	.data : {
		*(.data .data.*)
	}

	. = ALIGN(8);
	__bss_start = .;
	.bss : {
		*(.bss .bss.*)
		*(COMMON)
	}
	. = ALIGN(8);
	__bss_end = .;

	. = ALIGN(16);
	stos_stack_bottom = .;
	. += 0x10000;
	stos_stack_top = .;

	_end = .;
}
