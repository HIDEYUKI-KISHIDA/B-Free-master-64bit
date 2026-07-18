/*
 * Host-side ELF execution helper (fork + mprotect + entry).
 * Syscalls in the child hit the host Linux kernel; used until boot/trap lands.
 */
#ifndef BFREE_ELF_HOST_RUN_H
#define BFREE_ELF_HOST_RUN_H

#include "vmm.h"

#include <stddef.h>
#include <stdint.h>

int bfree_elf_host_exec(struct bfree_as *as, uintptr_t entry, size_t load_size,
			int *exit_status);

#endif
