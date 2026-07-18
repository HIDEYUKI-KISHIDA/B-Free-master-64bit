/*
 * P8_PAGING — identity page table build + ring-0 install skip on host.
 */
#include "paging.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct bfree_paging_state pg;
	int rc;

	bfree_paging_build_identity(&pg);
	CHECK(pg.built != 0, "built");
	CHECK(pg.pml4 != NULL && pg.pdpt != NULL && pg.pd != NULL, "tables");
	CHECK((pg.pml4[0] & 1) != 0, "pml4 present");
	CHECK((pg.pd[0] & (1ULL << 7)) != 0, "2M page");
	CHECK(pg.pd[0] == 0x83ULL, "first 2M identity");

	rc = bfree_paging_install(&pg);
	CHECK(rc == -2, "host install skipped");

	printf("P8_PAGING: PASS\n");
	return 0;
}
