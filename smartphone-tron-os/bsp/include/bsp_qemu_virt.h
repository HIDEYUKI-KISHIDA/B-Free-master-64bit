/*
 * STOS BSP — QEMU virt (AArch64) for Phase 2 UART bring-up.
 *
 * Machine: qemu-system-aarch64 -M virt -cpu cortex-a72
 * DRAM:    0x40000000
 * UART:    PL011 at 0x09000000
 * GIC:     GICv3 dist 0x08000000, redist 0x080A0000 (QEMU virt defaults)
 */

#ifndef STOS_BSP_QEMU_VIRT_H
#define STOS_BSP_QEMU_VIRT_H

#define STOS_DRAM_BASE            0x40000000ULL
#define STOS_DRAM_SIZE            0x08000000ULL /* 128 MiB default -m 128M */
#define STOS_KERNEL_LOAD_BASE     0x40000000ULL
#define STOS_GICD_BASE            0x08000000ULL
#define STOS_GICR_STRIDE          0x20000ULL
#define STOS_GICR_BASE(cpu)       (0x080A0000ULL + (unsigned long long)(cpu) * STOS_GICR_STRIDE)
#define STOS_UART_DBG_BASE        0x09000000ULL
#define STOS_UART_DBG_SIZE        0x1000ULL
#define STOS_TIMER_PNSIRQ         30U
#define STOS_TIMER_CLK_HZ         62500000U /* QEMU virt CNTFRQ typical; read CNTFRQ_EL0 at runtime */
#define STOS_CPU_COUNT            1U

#define STOS_PL011_DR             0x00U
#define STOS_PL011_FR             0x18U
#define STOS_PL011_FR_TXFF        (1U << 5)

#define STOS_BOARD_NAME           "qemu_virt"

#endif /* STOS_BSP_QEMU_VIRT_H */
