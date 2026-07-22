#ifndef BFREE_USER_BOOT_H
#define BFREE_USER_BOOT_H

#include <stddef.h>
#include <stdint.h>

#define BFREE_USER_LOAD_ADDR  0x400000UL
#define BFREE_USER_STACK_TOP  0x600000UL

int bfree_user_boot_exec(uintptr_t entry, uintptr_t user_stack);
int bfree_user_payload_install(const void *blob, size_t len);

#endif
