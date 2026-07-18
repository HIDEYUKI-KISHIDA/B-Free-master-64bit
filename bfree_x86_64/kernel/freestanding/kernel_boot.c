/*
 * Freestanding QEMU kernel entry (M8).
 */
#include "debugcon.h"
#include "initramfs.h"
#include "paging.h"
#include "trap_hw.h"
#include "trap_setup.h"

#include <stddef.h>

extern char _initramfs_start[];
extern char _initramfs_end[];

void bfree_kernel_boot(void)
{
	struct bfree_paging_state pg;

	bfree_paging_build_identity(&pg);
	bfree_paging_install(&pg);
	bfree_trap_init();
	bfree_trap_install();

	bfree_debug_puts("KERNEL_OK\n");

	if (bfree_initramfs_parse(_initramfs_start,
				  (size_t)(_initramfs_end - _initramfs_start)) == 0 &&
	    bfree_initramfs_file_count() > 0)
		bfree_debug_puts("INITRAMFS_OK\n");

	for (;;)
		__asm__ volatile("hlt");
}
