#ifndef CPU_INIT_H
#define CPU_INIT_H

#include <stdint.h>

#define NUM_IRQS 16
typedef void (*irq_handler_t)(void *regs, int irq, uint64_t errcode);

void register_irq_handler(int irq, irq_handler_t handler);

#endif // CPU_INIT_H
