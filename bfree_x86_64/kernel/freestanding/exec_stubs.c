#include "elf_host_run.h"
#include "elf_trap_exec.h"
#include "vmm.h"

#include <errno.h>

int bfree_elf_trap_exec(struct bfree_as *as, uintptr_t entry, size_t load_size,
			int *exit_status)
{
	(void)as;
	(void)entry;
	(void)load_size;
	(void)exit_status;
	return -ENOSYS;
}

int bfree_elf_host_exec(struct bfree_as *as, uintptr_t entry, size_t load_size,
			int *exit_status)
{
	(void)as;
	(void)entry;
	(void)load_size;
	(void)exit_status;
	return -ENOSYS;
}
