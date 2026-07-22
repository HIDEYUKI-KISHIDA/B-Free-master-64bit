/*
 * Freestanding QEMU kernel entry (M8–M14).
 */
#include "ash_guest_boot.h"
#include "debugcon.h"
#include "elf_user_load.h"
#include "gdt.h"
#include "guest_kernel.h"
#include "initramfs.h"
#include "paging.h"
#include "trap_hw.h"
#include "trap_setup.h"
#include "user_boot.h"

#include <stddef.h>

extern void bfree_syscall_insn_entry(void);

extern char _initramfs_start[];
extern char _initramfs_end[];

#ifndef BFREE_PREFER_STAT_GUEST_BOOT
#define BFREE_PREFER_STAT_GUEST_BOOT 0
#endif

#ifndef BFREE_PREFER_POLL_GUEST_BOOT
#define BFREE_PREFER_POLL_GUEST_BOOT 0
#endif

#ifndef BFREE_PREFER_LTP_OPEN_BOOT
#define BFREE_PREFER_LTP_OPEN_BOOT 0
#endif

#ifndef BFREE_PREFER_POSIX_IO_BOOT
#define BFREE_PREFER_POSIX_IO_BOOT 0
#endif

#ifndef BFREE_PREFER_BUSYBOX_BOOT
#define BFREE_PREFER_BUSYBOX_BOOT 0
#endif

#ifndef BFREE_PREFER_MUSL_BOOT
#define BFREE_PREFER_MUSL_BOOT 0
#endif

static int boot_elf_from_initramfs(const char *name)
{
	const void *payload;
	size_t payload_len;
	uintptr_t entry;

	if (bfree_initramfs_lookup(name, &payload, &payload_len) != 0)
		return 0;
	if (bfree_user_elf_install(payload, payload_len, &entry) != 0)
		return 0;
	bfree_user_boot_exec(entry, BFREE_USER_STACK_TOP);
	return 1;
}

static int boot_musl_elf(void)
{
	return boot_elf_from_initramfs("musl_static.elf");
}

static int boot_user_payload(void)
{
	const void *payload;
	size_t payload_len;

	if (bfree_initramfs_lookup("user_payload.bin", &payload,
				   &payload_len) != 0)
		return 0;
	if (bfree_user_payload_install(payload, payload_len) != 0)
		return 0;
	bfree_user_boot_exec(BFREE_USER_LOAD_ADDR, BFREE_USER_STACK_TOP);
	return 1;
}

static void boot_default_guest(void)
{
	if (!boot_user_payload())
		(void)boot_musl_elf();
}

void bfree_kernel_boot(void)
{
	struct bfree_paging_state pg;
	struct bfree_gdt_state gdt;

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

	bfree_kernel_guest_init();

#if BFREE_PREFER_STAT_GUEST_BOOT
	if (!boot_elf_from_initramfs("stat_guest.elf"))
		boot_default_guest();
#elif BFREE_PREFER_POLL_GUEST_BOOT
	if (!boot_elf_from_initramfs("poll_guest.elf"))
		boot_default_guest();
#elif BFREE_PREFER_LTP_OPEN_BOOT
	if (!boot_elf_from_initramfs("ltp_open_guest.elf"))
		boot_default_guest();
#elif BFREE_PREFER_POSIX_IO_BOOT
	if (!boot_elf_from_initramfs("posix_io_guest.elf"))
		boot_default_guest();
#elif BFREE_PREFER_BUSYBOX_BOOT
	if (!bfree_ash_guest_boot()) {
		if (!boot_musl_elf())
			(void)boot_user_payload();
	}
#elif BFREE_PREFER_MUSL_BOOT
	if (!boot_musl_elf())
		(void)boot_user_payload();
#else
	boot_default_guest();
#endif

	for (;;)
		__asm__ volatile("hlt");
}
