/*
 * x86_64 trap / IDT / syscall MSR setup (M7).
 */
#ifndef BFREE_TRAP_SETUP_H
#define BFREE_TRAP_SETUP_H

#include <stdint.h>

#define BFREE_IDT_ENTRIES 256

struct bfree_idt_gate {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t  ist;
	uint8_t  type_attr;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t zero;
} __attribute__((packed));

struct bfree_idt_ptr {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

struct bfree_trap_state {
	struct bfree_idt_gate gates[BFREE_IDT_ENTRIES];
	struct bfree_idt_ptr  idtr;
	uint64_t              syscall_star;
	uint64_t              syscall_lstar;
	uint64_t              syscall_fmask;
	int                   initialized;
};

void bfree_trap_init(void);
void bfree_trap_set_lstar(uintptr_t entry);
const struct bfree_trap_state *bfree_trap_state(void);
int bfree_trap_is_ready(void);

#endif
