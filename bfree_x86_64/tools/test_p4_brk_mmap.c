/*
 * P4_BRK_MMAP — guest heap via brk and anonymous mmap.
 */
#include "process.h"
#include "vmm.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct bfree_proc_mgr mgr;
	struct bfree_as *as;
	uintptr_t brk0;
	uintptr_t brk1;
	void *map;

	bfree_proc_init(&mgr);
	as = &bfree_proc_current(&mgr)->as;

	brk0 = bfree_brk(as, 0);
	CHECK(brk0 > 0, "initial brk");
	brk1 = bfree_brk(as, brk0 + 4096);
	CHECK(brk1 == brk0 + 4096, "brk grow");

	map = bfree_mmap(as, NULL, 8192, 3, BFREE_MMAP_PRIVATE | BFREE_MMAP_ANONYMOUS);
	CHECK(map != NULL, "mmap anon");
	((char *)map)[0] = 'M';
	CHECK(((char *)map)[0] == 'M', "mmap writable");

	printf("P4_BRK_MMAP: PASS\n");
	return 0;
}
