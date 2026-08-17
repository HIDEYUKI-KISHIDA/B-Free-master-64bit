#include "board.h"
#include "uart.h"

static inline void mmio_write32(unsigned long long addr, unsigned int val)
{
	*(volatile unsigned int *)addr = val;
}

static inline unsigned int mmio_read32(unsigned long long addr)
{
	return *(volatile unsigned int *)addr;
}

void stos_uart_init(void)
{
	/* kexec/QEMU: UART already programmed. TX-poll only (spec §6.5). */
}

#if defined(STOS_BOARD_QEMU_VIRT)

void stos_uart_putc(char c)
{
	unsigned long long uart = STOS_UART_DBG_BASE;

	if (c == '\n')
		stos_uart_putc('\r');
	while (mmio_read32(uart + STOS_PL011_FR) & STOS_PL011_FR_TXFF)
		;
	mmio_write32(uart + STOS_PL011_DR, (unsigned char)c);
}

#elif defined(STOS_BOARD_LENA)

void stos_uart_putc(char c)
{
	unsigned long long uart = STOS_UART_DBG_BASE;
	unsigned int spins;

	if (c == '\n')
		stos_uart_putc('\r');
	/* Wait until TX FIFO word count is 0, then push one byte. */
	for (spins = 0; spins < 1000000U; spins++) {
		unsigned int wc = mmio_read32(uart + STOS_GENI_TX_FIFO_STATUS) &
				  STOS_GENI_TX_FIFO_WC_MASK;
		if (wc == 0U)
			break;
	}
	mmio_write32(uart + STOS_GENI_TX_FIFOn, (unsigned char)c);
}

#endif

void stos_uart_puts(const char *s)
{
	while (*s)
		stos_uart_putc(*s++);
}
