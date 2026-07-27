/*
 * P20_USER_VA_BRK_MMAP — identity-mapped user VA heap (Linux-on-BTRON L2).
 */
#include "vmm.h"

#include <stdio.h>
#include <stdint.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct bfree_as as;
	uintptr_t brk0;
	uintptr_t brk1;
	void *map;

	/* A: host offset mode still works */
	CHECK(bfree_as_init(&as, 65536) == 0, "host as init");
	brk0 = bfree_brk(&as, 0);
	CHECK(brk0 == 4096, "host initial brk");
	brk1 = bfree_brk(&as, brk0 + 4096);
	CHECK(brk1 == brk0 + 4096, "host brk grow");
	map = bfree_mmap(&as, NULL, 8192, 3,
			 BFREE_MMAP_PRIVATE | BFREE_MMAP_ANONYMOUS);
	CHECK(map != NULL, "host mmap");
	((char *)map)[0] = 'H';
	CHECK(((char *)map)[0] == 'H', "host mmap write");
	bfree_as_free(&as);

	/* B: user VA absolute mode (address math; page zeroing is guest-only) */
	CHECK(bfree_as_init_user_va(&as, BFREE_USER_HEAP_BASE,
				    BFREE_USER_MMAP_TOP,
				    BFREE_USER_HEAP_BASE) == 0,
	      "user va init");
	CHECK(as.va_abs == 1, "va_abs set");
	brk0 = bfree_brk(&as, 0);
	CHECK(brk0 == BFREE_USER_HEAP_BASE, "user brk query");
	brk1 = bfree_brk(&as, brk0 + 8192);
	CHECK(brk1 == brk0 + 8192, "user brk grow");
	CHECK(brk1 < BFREE_USER_MMAP_TOP, "brk below mmap top");

	map = bfree_mmap(&as, NULL, 4096, 3,
			 BFREE_MMAP_PRIVATE | BFREE_MMAP_ANONYMOUS);
	CHECK(map != NULL, "user mmap");
	CHECK((uintptr_t)map < BFREE_USER_MMAP_TOP, "mmap below top");
	CHECK((uintptr_t)map >= brk1, "mmap above brk");
	CHECK(((uintptr_t)map & 0xFFF) == 0, "mmap page aligned");

	CHECK(bfree_brk(&as, (uintptr_t)map + 1) == brk1,
	      "brk collision unchanged");

	bfree_as_free(&as);
	printf("P20_USER_VA_BRK_MMAP: PASS\n");
	return 0;
}
