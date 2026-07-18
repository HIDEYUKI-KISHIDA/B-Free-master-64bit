/*
 * Static payload using trap entry for write + exit (M7).
 */
#include "syscall.h"
#include "trap_setup.h"

#include <unistd.h>

static long trap_invoke(unsigned long nr, unsigned long a0, unsigned long a1,
			unsigned long a2)
{
	unsigned long ret;

	__asm__ volatile(
		"mov %[nr], %%rax\n"
		"mov %[a0], %%rdi\n"
		"mov %[a1], %%rsi\n"
		"mov %[a2], %%rdx\n"
		"xor %%r10, %%r10\n"
		"xor %%r8, %%r8\n"
		"xor %%r9, %%r9\n"
		"call bfree_trap_syscall_entry\n"
		"mov %%rax, %[ret]\n"
		: [ret] "=r"(ret)
		: [nr] "r"(nr), [a0] "r"(a0), [a1] "r"(a1), [a2] "r"(a2)
		: "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx",
		  "memory");

	return (long)ret;
}

int main(void)
{
	const char msg[] = "TRAP_OK\n";

	guest_init();
	bfree_trap_init();
	trap_invoke(1, 1, (unsigned long)msg, sizeof(msg) - 1);
	trap_invoke(60, 0, 0, 0);
	return 0;
}
