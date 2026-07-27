	.section .note.GNU-stack,"",@progbits
	.section .rodata
	.align 8
	.global _initramfs_start
_initramfs_start:
	.incbin "build/initramfs.cpio"
	.global _initramfs_end
_initramfs_end:
