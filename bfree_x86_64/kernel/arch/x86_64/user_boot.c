#include "user_boot.h"

static int in_ring0(void)
{
	uint16_t cs;

	__asm__ volatile("mov %%cs, %0" : "=r"(cs));
	return (cs & 3U) == 0U;
}

int bfree_user_boot_exec(uintptr_t entry, uintptr_t user_stack)
{
	(void)entry;
	(void)user_stack;

	if (!in_ring0())
		return -2;

	/* M9 home-PC task: implement iretq in user_boot_ring3.S */
	return -3;
}
