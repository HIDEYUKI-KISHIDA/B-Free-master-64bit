/*
 * P9_GDT — GDT table build + ring-0 install skip on host.
 */
#include "gdt.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct bfree_gdt_state gdt;
	int rc;

	bfree_gdt_build(&gdt);
	CHECK(gdt.built != 0, "built");
	CHECK(gdt.entries[1].access == 0x9a, "kcode access");
	CHECK(gdt.entries[3].access == 0xfa, "ucode access");
	CHECK(gdt.gdtr.limit > 0, "gdtr limit");

	rc = bfree_gdt_install(&gdt);
	CHECK(rc == -2, "host install skipped");

	printf("P9_GDT: PASS\n");
	return 0;
}
