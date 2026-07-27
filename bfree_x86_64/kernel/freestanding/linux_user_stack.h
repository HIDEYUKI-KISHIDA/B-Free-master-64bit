/*
 * Build a Linux x86_64 initial user stack (argc/argv/envp/auxv).
 */
#ifndef BFREE_LINUX_USER_STACK_H
#define BFREE_LINUX_USER_STACK_H

#include <stddef.h>
#include <stdint.h>

/* auxv tags (linux/auxvec.h) */
#define BFREE_AT_NULL    0
#define BFREE_AT_PHDR    3
#define BFREE_AT_PHENT   4
#define BFREE_AT_PHNUM   5
#define BFREE_AT_PAGESZ  6
#define BFREE_AT_BASE    7
#define BFREE_AT_FLAGS   8
#define BFREE_AT_ENTRY   9
#define BFREE_AT_UID     11
#define BFREE_AT_EUID    12
#define BFREE_AT_GID     13
#define BFREE_AT_EGID    14
#define BFREE_AT_SECURE  23
#define BFREE_AT_RANDOM  25

struct bfree_linux_auxinfo {
	uintptr_t phdr;
	unsigned  phent;
	unsigned  phnum;
	uintptr_t entry;
};

/*
 * Build stack growing down from stack_top (exclusive).
 * Returns new RSP (16-byte aligned), or 0 on failure.
 * Strings and vectors are placed below stack_top inside the same region.
 */
uintptr_t bfree_linux_user_stack_build(uintptr_t stack_top, int argc,
				       char *const argv[], char *const envp[],
				       const struct bfree_linux_auxinfo *aux);

#endif
