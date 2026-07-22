#include "gdt.h"

#include <stddef.h>
#include <string.h>

static void gdt_set(struct bfree_gdt_entry *e, uint32_t base, uint32_t limit,
		    uint8_t access, uint8_t gran)
{
	e->limit_low = (uint16_t)(limit & 0xffff);
	e->base_low = (uint16_t)(base & 0xffff);
	e->base_mid = (uint8_t)((base >> 16) & 0xff);
	e->access = access;
	e->limit_high = (uint8_t)((limit >> 16) & 0x0f);
	e->granularity = (uint8_t)(gran & 0xf0);
	e->base_high = (uint8_t)((base >> 24) & 0xff);
}

static int in_ring0(void)
{
	uint16_t cs;

	__asm__ volatile("mov %%cs, %0" : "=r"(cs));
	return (cs & 3U) == 0U;
}

void bfree_gdt_build(struct bfree_gdt_state *st)
{
	if (st == NULL)
		return;

	memset(st, 0, sizeof(*st));
	gdt_set(&st->entries[1], 0, 0xfffff, 0x9a, 0xaf); /* kernel code */
	gdt_set(&st->entries[2], 0, 0xfffff, 0x92, 0xcf); /* kernel data */
	gdt_set(&st->entries[3], 0, 0xfffff, 0xf2, 0xcf); /* user data  0x18 */
	gdt_set(&st->entries[4], 0, 0xfffff, 0xfa, 0xaf); /* user code  0x20 */
	st->gdtr.limit = (uint16_t)(sizeof(st->entries) - 1);
	st->gdtr.base = (uint64_t)(uintptr_t)st->entries;
	st->built = 1;
}

int bfree_gdt_install(const struct bfree_gdt_state *st)
{
	if (st == NULL || !st->built)
		return -1;
	if (!in_ring0())
		return -2;

	__asm__ volatile("lgdt %0" : : "m"(st->gdtr));
	return 0;
}
