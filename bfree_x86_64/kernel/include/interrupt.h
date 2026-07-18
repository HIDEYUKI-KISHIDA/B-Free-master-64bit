#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>

static inline uint64_t save_and_disable_interrupts(void) {
    uint64_t rflags;
    __asm__ volatile (
        "pushfq\n\t"
        "pop %0\n\t"
        "cli"
        : "=rm"(rflags) : : "memory"
    );
    return rflags;
}

static inline void restore_interrupts(uint64_t rflags) {
    __asm__ volatile (
        "push %0\n\t"
        "popfq"
        : : "rm"(rflags) : "memory"
    );
}

#define BEGIN_CRITICAL_SECTION  { uint64_t _saved_rflags = save_and_disable_interrupts();
#define END_CRITICAL_SECTION    restore_interrupts(_saved_rflags); }

#endif // INTERRUPT_H
