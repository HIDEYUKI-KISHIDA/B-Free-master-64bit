#include "board.h"
#include "uart.h"

void stos_main(void)
{
	stos_uart_init();
	stos_uart_puts(STOS_BANNER);

#if defined(STOS_BOARD_QEMU_VIRT)
	/* PSCI SYSTEM_OFF (0x84000008) so QEMU exits without SIGKILL. */
	__asm__ volatile(
		"movz x0, #0x0008\n"
		"movk x0, #0x8400, lsl #16\n"
		"smc #0"
		:
		:
		: "x0", "x1", "x2", "x3", "memory");
#endif
	for (;;)
		__asm__ volatile("wfi");
}
