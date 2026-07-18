/*
 * Guest address-space helpers (M2 fork/exec, M4 brk/mmap).
 */
#ifndef BFREE_VMM_H
#define BFREE_VMM_H

#include <stddef.h>
#include <stdint.h>

#define BFREE_MMAP_ANONYMOUS 0x20
#define BFREE_MMAP_PRIVATE   0x02
#define BFREE_MMAP_SHARED    0x01

struct bfree_as {
	uint8_t *mem;
	size_t   size;
	size_t   brk_end;
	size_t   mmap_next;
};

int  bfree_as_init(struct bfree_as *as, size_t size);
void bfree_as_free(struct bfree_as *as);
int  bfree_as_fork_copy(struct bfree_as *child, const struct bfree_as *parent);
uint8_t *bfree_as_ptr(struct bfree_as *as);

uintptr_t bfree_brk(struct bfree_as *as, uintptr_t new_brk);
void *bfree_mmap(struct bfree_as *as, void *addr, size_t len, int prot,
		 int flags);

#endif /* BFREE_VMM_H */
