/*
 * Freestanding QEMU kernel entry (M8–M9).
 */
#include "debugcon.h"
#include "gdt.h"
#include "initramfs.h"
#include "paging.h"
#include "trap_hw.h"
#include "trap_setup.h"
#include "user_boot.h"

#include <stddef.h>

extern void bfree_syscall_insn_entry(void);

extern char _initramfs_start[];
extern char _initramfs_end[];

void bfree_kernel_boot(void)
{
	struct bfree_paging_state pg;
	struct bfree_gdt_state gdt;
	const void *payload;
	size_t payload_len;

	bfree_paging_build_identity(&pg);
	bfree_paging_install(&pg);
	bfree_gdt_build(&gdt);
	bfree_gdt_install(&gdt);
	bfree_trap_init();
	bfree_trap_set_lstar((uintptr_t)bfree_syscall_insn_entry);
	bfree_trap_install();

	bfree_debug_puts("KERNEL_OK\n");

	if (bfree_initramfs_parse(_initramfs_start,
				  (size_t)(_initramfs_end - _initramfs_start)) == 0 &&
	    bfree_initramfs_file_count() > 0)
		bfree_debug_puts("INITRAMFS_OK\n");

	if (bfree_initramfs_lookup("user_payload.bin", &payload,
				   &payload_len) == 0 &&
	    bfree_user_payload_install(payload, payload_len) == 0)
		bfree_user_boot_exec(BFREE_USER_LOAD_ADDR, BFREE_USER_STACK_TOP);

	for (;;)
		__asm__ volatile("hlt");
}
