#include "trap_setup.h"

#include <string.h>

static struct bfree_trap_state bfree_trap;

static void idt_set_gate(int vec, uintptr_t handler)
{
	struct bfree_idt_gate *g = &bfree_trap.gates[vec];

	g->offset_low = (uint16_t)(handler & 0xffff);
	g->selector = 0x08;
	g->ist = 0;
	g->type_attr = 0x8e;
	g->offset_mid = (uint16_t)((handler >> 16) & 0xffff);
	g->offset_high = (uint32_t)((handler >> 32) & 0xffffffff);
	g->zero = 0;
}

void bfree_trap_init(void)
{
	extern void bfree_trap_syscall_entry(void);

	memset(&bfree_trap, 0, sizeof(bfree_trap));
	idt_set_gate(0x80, (uintptr_t)bfree_trap_syscall_entry);
	bfree_trap.idtr.limit = (uint16_t)(sizeof(bfree_trap.gates) - 1);
	bfree_trap.idtr.base = (uint64_t)(uintptr_t)bfree_trap.gates;
	/* kernel CS 0x08; SYSRET base 0x10 -> SS=0x18|3, CS=0x20|3 */
	bfree_trap.syscall_star = ((uint64_t)0x08 << 32) | ((uint64_t)0x10 << 48);
	bfree_trap.syscall_lstar = (uint64_t)(uintptr_t)bfree_trap_syscall_entry;
	bfree_trap.syscall_fmask = 0x200;
	bfree_trap.initialized = 1;
}

void bfree_trap_set_lstar(uintptr_t entry)
{
	bfree_trap.syscall_lstar = (uint64_t)entry;
}

const struct bfree_trap_state *bfree_trap_state(void)
{
	return &bfree_trap;
}

int bfree_trap_is_ready(void)
{
	return bfree_trap.initialized != 0;
}
