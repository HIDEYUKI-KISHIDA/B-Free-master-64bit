/*
 * Guest address-space helpers (M2 fork/exec, M4 brk/mmap, M21 L2 user VA).
 */
#ifndef BFREE_VMM_H
#define BFREE_VMM_H

#include <stddef.h>
#include <stdint.h>

#define BFREE_MMAP_ANONYMOUS 0x20
#define BFREE_MMAP_PRIVATE   0x02
#define BFREE_MMAP_SHARED    0x01

/* Ring-3 identity-map layout (Linux-on-BTRON L2). */
#define BFREE_USER_HEAP_BASE   0x00540000UL
#define BFREE_USER_MMAP_TOP    0x01F00000UL
#define BFREE_USER_VA_STACK    0x02000000UL

struct bfree_as {
	uint8_t *mem;           /* host calloc arena; unused in va_abs mode */
	size_t   size;
	uintptr_t brk_start;    /* absolute VA or host-offset base */
	uintptr_t brk_end;
	uintptr_t mmap_next;    /* next mmap bump (grows up in host, down in va_abs) */
	uintptr_t mmap_limit;   /* exclusive limit for mmap */
	int       va_abs;       /* 1: brk/mmap return identity-mapped user VAs */
};

int  bfree_as_init(struct bfree_as *as, size_t size);
int  bfree_as_init_user_va(struct bfree_as *as, uintptr_t heap_base,
			   uintptr_t mmap_top, uintptr_t heap_ceil);
void bfree_as_free(struct bfree_as *as);
int  bfree_as_fork_copy(struct bfree_as *child, const struct bfree_as *parent);
uint8_t *bfree_as_ptr(struct bfree_as *as);

uintptr_t bfree_brk(struct bfree_as *as, uintptr_t new_brk);
void *bfree_mmap(struct bfree_as *as, void *addr, size_t len, int prot,
		 int flags);
int bfree_munmap(struct bfree_as *as, void *addr, size_t len);
int bfree_mprotect(struct bfree_as *as, void *addr, size_t len, int prot);
int bfree_madvise(struct bfree_as *as, void *addr, size_t len, int advice);

#endif /* BFREE_VMM_H */
