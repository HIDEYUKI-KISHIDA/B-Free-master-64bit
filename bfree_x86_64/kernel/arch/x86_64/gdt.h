#ifndef BFREE_GDT_H
#define BFREE_GDT_H

#include <stdint.h>

#define BFREE_GDT_ENTRIES 5

/* Selectors (RPL 0 for kernel, user segments use RPL 3 at runtime). */
#define BFREE_SEL_NULL   0x00
#define BFREE_SEL_KCODE  0x08
#define BFREE_SEL_KDATA  0x10
#define BFREE_SEL_UCODE  0x18
#define BFREE_SEL_UDATA  0x20

struct bfree_gdt_entry {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t  base_mid;
	uint8_t  access;
	uint8_t  limit_high : 4;
	uint8_t  granularity : 4;
	uint8_t  base_high;
} __attribute__((packed));

struct bfree_gdt_ptr {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

struct bfree_gdt_state {
	struct bfree_gdt_entry entries[BFREE_GDT_ENTRIES];
	struct bfree_gdt_ptr   gdtr;
	int                    built;
};

void bfree_gdt_build(struct bfree_gdt_state *st);
int bfree_gdt_install(const struct bfree_gdt_state *st);

#endif
