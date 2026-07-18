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
	return 0;
}

void bfree_as_free(struct bfree_as *as)
{
	if (as == NULL)
		return;
	free(as->mem);
	as->mem = NULL;
	as->size = 0;
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
	return 0;
}

uint8_t *bfree_as_ptr(struct bfree_as *as)
{
	return as != NULL ? as->mem : NULL;
}
