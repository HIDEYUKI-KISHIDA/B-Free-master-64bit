#ifndef INTERRUPT_MAIN_H
#define INTERRUPT_MAIN_H
#include <stdint.h>
void knl_interrupt_main(uint64_t *regs, uint64_t vecno);
#endif
