/*
 * Boot BusyBox ash from initramfs via Linux process ABI (stack+auxv).
 */
#include "ash_guest_boot.h"
#include "debugcon.h"
#include "elf_user_load.h"
#include "initramfs.h"
#include "linux_user_stack.h"
#include "procfs.h"
#include "syscall.h"
#include "user_boot.h"
#include "vmm.h"

#include <stddef.h>
#include <stdint.h>

#define ASH_GUEST_BUSYBOX_PATH "bin/busybox"

int bfree_ash_guest_boot(void)
{
	const void *payload;
	size_t payload_len;
	uintptr_t entry;
	uintptr_t rsp;
	struct bfree_linux_auxinfo aux;
	char *argv[] = {
		"/bin/busybox", "ash", "-c", "echo ASH_GUEST_OK", NULL
	};

	if (bfree_initramfs_lookup(ASH_GUEST_BUSYBOX_PATH, &payload,
				   &payload_len) != 0)
		return 0;
	if (bfree_user_elf_install_ex(payload, payload_len, &entry, &aux) != 0)
		return 0;

	rsp = bfree_linux_user_stack_build(BFREE_USER_STACK_TOP, 4, argv, NULL,
					   &aux);
	if (rsp == 0)
		return 0;

	{
		struct bfree_fs *fs = guest_fs();

		if (fs != NULL)
			(void)bfree_procfs_on_exec(fs, "/bin/busybox",
						   BFREE_USER_HEAP_BASE,
						   BFREE_USER_STACK_TOP);
	}

	/* Jump to real ELF e_entry with Linux stack — no C-ABI trampoline. */
	bfree_debug_puts("ASH_LINUX_STACK_BOOT\n");
	bfree_user_boot_exec(entry, rsp);
	return 1;
}
