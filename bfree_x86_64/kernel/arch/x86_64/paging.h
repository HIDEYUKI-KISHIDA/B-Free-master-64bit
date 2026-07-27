#ifndef BFREE_PAGING_H
#define BFREE_PAGING_H

#include <stdint.h>

#define BFREE_IDENTITY_MAP_BYTES (128UL * 1024UL * 1024UL)

struct bfree_paging_state {
	uint64_t *pml4;
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t  cr3;
	int       built;
};

void bfree_paging_build_identity(struct bfree_paging_state *st);
int bfree_paging_install(const struct bfree_paging_state *st);
const struct bfree_paging_state *bfree_paging_state(void);

#endif
