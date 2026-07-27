/*
 * Boot BusyBox ash from initramfs on the QEMU guest (M12).
 */
#include "ash_guest_boot.h"
#include "elf_user_load.h"
#include "initramfs.h"
#include "user_boot.h"

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, unsigned long n);

#define ASH_GUEST_TRAMP_ADDR  0x100000UL
#define ASH_GUEST_BUSYBOX_PATH "bin/busybox"

extern char ash_guest_tramp[];
extern char ash_guest_tramp_end[];
extern uint64_t ash_guest_entry_slot;

int bfree_ash_guest_boot(void)
{
	const void *payload;
	size_t payload_len;
	uintptr_t entry;
	uintptr_t entry_off;
	uint64_t *entry_patch;

	if (bfree_initramfs_lookup(ASH_GUEST_BUSYBOX_PATH, &payload,
				   &payload_len) != 0)
		return 0;
	if (bfree_user_elf_install(payload, payload_len, &entry) != 0)
		return 0;

	memcpy((void *)ASH_GUEST_TRAMP_ADDR, ash_guest_tramp,
	       (unsigned long)(ash_guest_tramp_end - ash_guest_tramp));

	entry_off = (uintptr_t)(uintptr_t)&ash_guest_entry_slot -
		    (uintptr_t)(uintptr_t)&ash_guest_tramp;
	entry_patch = (uint64_t *)(ASH_GUEST_TRAMP_ADDR + entry_off);
	*entry_patch = (uint64_t)entry;

	bfree_user_boot_exec(ASH_GUEST_TRAMP_ADDR, BFREE_USER_STACK_TOP);
	return 1;
}
