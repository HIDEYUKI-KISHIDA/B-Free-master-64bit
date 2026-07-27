#ifndef BFREE_ELF_USER_LOAD_H
#define BFREE_ELF_USER_LOAD_H

#include <stddef.h>
#include <stdint.h>

int bfree_user_elf_install(const void *blob, size_t len, uintptr_t *entry_out);

#endif
