/*
 * cpl_check.c - x86_64 CPL判定ユーティリティ
 * 仕様: TK2_x86_64_Spec.md v2.4準拠
 */
#include <stdint.h>

// 現在のCPL (Current Privilege Level) を返す
// CSセグメントレジスタの下位2bit [1:0] を参照
static inline uint8_t get_cpl(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs & 0x3;
}

// RFLAGS.IOPLはI/O権限でありCPLとは別物
static inline uint8_t get_iopl(void) {
    uint64_t rflags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(rflags));
    return (rflags >> 12) & 0x3;
}
