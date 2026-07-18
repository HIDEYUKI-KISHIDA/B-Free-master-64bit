#include "vmm.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int bfree_as_init(struct bfree_as *as, size_t size)
{
	if (as == NULL || size == 0)
		return -EINVAL;
	as->mem = calloc(1, size);
	if (as->mem == NULL)
		return -ENOMEM;
	as->size = size;
	as->brk_end = 4096;
	as->mmap_next = 8192;
	return 0;
}

void bfree_as_free(struct bfree_as *as)
{
	if (as == NULL)
		return;
	free(as->mem);
	as->mem = NULL;
	as->size = 0;
	as->brk_end = 0;
}

int bfree_as_fork_copy(struct bfree_as *child, const struct bfree_as *parent)
{
	if (child == NULL || parent == NULL || parent->mem == NULL)
		return -EINVAL;
	bfree_as_free(child);
	child->mem = malloc(parent->size);
	if (child->mem == NULL)
		return -ENOMEM;
	memcpy(child->mem, parent->mem, parent->size);
	child->size = parent->size;
	child->brk_end = parent->brk_end;
	child->mmap_next = parent->mmap_next;
	return 0;
}

uint8_t *bfree_as_ptr(struct bfree_as *as)
{
	return as != NULL ? as->mem : NULL;
}

uintptr_t bfree_brk(struct bfree_as *as, uintptr_t new_brk)
{
	if (as == NULL || as->mem == NULL)
		return 0;
	if (new_brk == 0)
		return as->brk_end;
	if (new_brk < 4096 || new_brk > as->size)
		return (uintptr_t)-1;
	as->brk_end = new_brk;
	return as->brk_end;
}

void *bfree_mmap(struct bfree_as *as, void *addr, size_t len, int prot,
		 int flags)
{
	void *base;

	(void)prot;
	(void)addr;
	if (as == NULL || as->mem == NULL || len == 0)
		return NULL;
	if (!(flags & BFREE_MMAP_ANONYMOUS))
		return NULL;
	if (as->mmap_next + len > as->size)
		return NULL;
	base = as->mem + as->mmap_next;
	memset(base, 0, len);
	as->mmap_next += (len + 4095) & ~(size_t)4095;
	(void)flags;
	return base;
}
