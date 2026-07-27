/*
 * Kernel entry for hosted harness and future QEMU boot (M7).
 */
#include "kernel_main.h"
#include "syscall.h"
#include "trap_setup.h"

void bfree_kernel_main(void)
{
	const char msg[] = "KERNEL_OK\n";

	bfree_trap_init();
	guest_init();
	bfree_invoke_syscall(1, 1, (unsigned long)msg, sizeof(msg) - 1, 0, 0, 0);
}
