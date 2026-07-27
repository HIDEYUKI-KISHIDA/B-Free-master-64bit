/*
 * Host stubs so elf_user_exec.c can link into P21 without ring-3 boot deps.
 */
#include "elf_user_load.h"
#include "user_boot.h"

#include <stddef.h>
#include <stdint.h>

int bfree_user_elf_install_ex(const void *blob, size_t len, uintptr_t *entry,
			      struct bfree_linux_auxinfo *aux)
{
	(void)blob;
	(void)len;
	(void)entry;
	(void)aux;
	return -1;
}

int bfree_user_boot_exec(uintptr_t entry, uintptr_t stack)
{
	(void)entry;
	(void)stack;
	return 0;
}
