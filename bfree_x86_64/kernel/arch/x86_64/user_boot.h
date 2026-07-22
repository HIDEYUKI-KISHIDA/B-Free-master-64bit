#ifndef BFREE_USER_BOOT_H
#define BFREE_USER_BOOT_H

#include <stdint.h>

/*
 * Launch ring-3 at entry with user stack (M9).
 * Returns 0 on success; -2 if not in ring 0 (host harness skip).
 */
int bfree_user_boot_exec(uintptr_t entry, uintptr_t user_stack);

#endif
