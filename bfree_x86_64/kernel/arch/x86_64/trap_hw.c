#include "trap_hw.h"
#include "trap_setup.h"

#define MSR_STAR   0xc0000081
#define MSR_LSTAR  0xc0000082
#define MSR_FMASK  0xc0000084

static int hw_installed;

static int in_ring0(void)
{
	uint16_t cs;

	__asm__ volatile("mov %%cs, %0" : "=r"(cs));
	return (cs & 3U) == 0U;
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
	uint32_t lo = (uint32_t)value;
	uint32_t hi = (uint32_t)(value >> 32);

	__asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

int bfree_trap_install(void)
{
	const struct bfree_trap_state *st;

	if (!bfree_trap_is_ready())
		return -1;
	if (!in_ring0())
		return -2;

	st = bfree_trap_state();
	__asm__ volatile("lidt %0" : : "m"(st->idtr));
	wrmsr(MSR_STAR, st->syscall_star);
	wrmsr(MSR_LSTAR, st->syscall_lstar);
	wrmsr(MSR_FMASK, st->syscall_fmask);
	hw_installed = 1;
	return 0;
}

int bfree_trap_hw_installed(void)
{
	return hw_installed;
}
