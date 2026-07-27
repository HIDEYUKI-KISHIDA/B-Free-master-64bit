/*
 * ELF execution via trap dispatch in child (M7).
 */
#ifndef BFREE_ELF_TRAP_EXEC_H
#define BFREE_ELF_TRAP_EXEC_H

#include "vmm.h"

#include <stddef.h>
#include <stdint.h>

int bfree_elf_trap_exec(struct bfree_as *as, uintptr_t entry, size_t load_size,
			int *exit_status);

#endif
