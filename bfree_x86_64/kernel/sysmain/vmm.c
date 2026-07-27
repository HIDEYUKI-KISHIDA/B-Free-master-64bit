#include "vmm.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static size_t page_up(size_t n)
{
	return (n + 4095UL) & ~(size_t)4095UL;
}

int bfree_as_init(struct bfree_as *as, size_t size)
{
	if (as == NULL || size == 0)
		return -EINVAL;
	memset(as, 0, sizeof(*as));
	as->mem = calloc(1, size);
	if (as->mem == NULL)
		return -ENOMEM;
	as->size = size;
	as->brk_start = 4096;
	as->brk_end = 4096;
	as->mmap_next = 8192;
	as->mmap_limit = size;
	as->va_abs = 0;
	return 0;
}

int bfree_as_init_user_va(struct bfree_as *as, uintptr_t heap_base,
			  uintptr_t mmap_top, uintptr_t heap_ceil)
{
	if (as == NULL || heap_base == 0 || mmap_top <= heap_base)
		return -EINVAL;
	bfree_as_free(as);
	memset(as, 0, sizeof(*as));
	as->mem = NULL;
	as->size = 0;
	as->brk_start = heap_base;
	as->brk_end = heap_base;
	/* mmap grows down from mmap_top toward heap_ceil (or brk). */
	as->mmap_next = mmap_top;
	as->mmap_limit = heap_ceil ? heap_ceil : heap_base;
	as->va_abs = 1;
	return 0;
}

void bfree_as_free(struct bfree_as *as)
{
	if (as == NULL)
		return;
	free(as->mem);
	as->mem = NULL;
	as->size = 0;
	as->brk_start = 0;
	as->brk_end = 0;
	as->mmap_next = 0;
	as->mmap_limit = 0;
	as->va_abs = 0;
}

int bfree_as_fork_copy(struct bfree_as *child, const struct bfree_as *parent)
{
	if (child == NULL || parent == NULL)
		return -EINVAL;
	if (parent->va_abs) {
		bfree_as_free(child);
		*child = *parent;
		child->mem = NULL;
		return 0;
	}
	if (parent->mem == NULL)
		return -EINVAL;
	bfree_as_free(child);
	child->mem = malloc(parent->size);
	if (child->mem == NULL)
		return -ENOMEM;
	memcpy(child->mem, parent->mem, parent->size);
	child->size = parent->size;
	child->brk_start = parent->brk_start;
	child->brk_end = parent->brk_end;
	child->mmap_next = parent->mmap_next;
	child->mmap_limit = parent->mmap_limit;
	child->va_abs = 0;
	return 0;
}

uint8_t *bfree_as_ptr(struct bfree_as *as)
{
	return as != NULL ? as->mem : NULL;
}

uintptr_t bfree_brk(struct bfree_as *as, uintptr_t new_brk)
{
	if (as == NULL)
		return 0;

	if (as->va_abs) {
		uintptr_t old = as->brk_end;

		if (new_brk == 0)
			return old;
		/* Grow up toward the current mmap frontier. */
		if (new_brk < as->brk_start || new_brk > as->mmap_next)
			return old;
#ifdef BFREE_KERNEL_GUEST
		if (new_brk > old)
			memset((void *)old, 0, (size_t)(new_brk - old));
#endif
		as->brk_end = new_brk;
		return as->brk_end;
	}

	if (as->mem == NULL)
		return 0;
	if (new_brk == 0)
		return as->brk_end;
	if (new_brk < as->brk_start || new_brk > as->size)
		return (uintptr_t)-1;
	as->brk_end = new_brk;
	return as->brk_end;
}

void *bfree_mmap(struct bfree_as *as, void *addr, size_t len, int prot,
		 int flags)
{
	size_t need;
	void *base;

	(void)prot;
	(void)addr;
	if (as == NULL || len == 0)
		return NULL;
	if (!(flags & BFREE_MMAP_ANONYMOUS))
		return NULL;
	need = page_up(len);

	if (as->va_abs) {
		uintptr_t start;

		if (need > as->mmap_next)
			return NULL;
		start = as->mmap_next - need;
		if (start < as->brk_end || start < as->mmap_limit)
			return NULL;
#ifdef BFREE_KERNEL_GUEST
		memset((void *)start, 0, need);
#endif
		as->mmap_next = start;
		return (void *)start;
	}

	if (as->mem == NULL)
		return NULL;
	if (as->mmap_next + need > as->mmap_limit)
		return NULL;
	base = as->mem + as->mmap_next;
	memset(base, 0, len);
	as->mmap_next += need;
	return base;
}

int bfree_munmap(struct bfree_as *as, void *addr, size_t len)
{
	uintptr_t uaddr;
	size_t need;

	if (as == NULL || addr == NULL || len == 0)
		return -EINVAL;
	need = page_up(len);
	uaddr = (uintptr_t)addr;

	if (as->va_abs) {
		/* Reclaim only if unmapping the current mmap frontier. */
		if (uaddr == as->mmap_next)
			as->mmap_next = uaddr + need;
		return 0;
	}

	if (as->mem == NULL)
		return -EINVAL;
	{
		uintptr_t off = (uintptr_t)((uint8_t *)addr - as->mem);

		if (off >= as->size)
			return -EINVAL;
		if (off + need >= as->mmap_next)
			as->mmap_next = off;
	}
	return 0;
}

int bfree_mprotect(struct bfree_as *as, void *addr, size_t len, int prot)
{
	(void)prot;
	if (as == NULL || addr == NULL || len == 0)
		return -EINVAL;
	if (as->va_abs) {
		uintptr_t u = (uintptr_t)addr;

		if (u < as->brk_start || u + len > BFREE_USER_VA_STACK)
			return -ENOMEM;
		return 0;
	}
	if (as->mem == NULL)
		return -EINVAL;
	if ((uint8_t *)addr < as->mem ||
	    (uint8_t *)addr + len > as->mem + as->size)
		return -ENOMEM;
	return 0;
}

int bfree_madvise(struct bfree_as *as, void *addr, size_t len, int advice)
{
	(void)advice;
	return bfree_mprotect(as, addr, len, 0);
}
