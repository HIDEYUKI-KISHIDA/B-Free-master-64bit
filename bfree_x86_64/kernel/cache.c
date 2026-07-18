#include <stdint.h>
#include <stddef.h>

/**
 * 指定レンジのキャッシュをフラッシュする (CLFLUSH)
 */
void cache_flush_range(void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr & ~(64 - 1);
    uintptr_t end = (uintptr_t)addr + size;

    for (uintptr_t p = start; p < end; p += 64) {
        __asm__ volatile("clflush (%0)" : : "r"(p) : "memory");
    }
    __asm__ volatile("sfence" : : : "memory");
}

/**
 * 全キャッシュの無効化と書き戻し (WBINVD) - 特権モード用
 */
void cache_flush_all(void) {
    __asm__ volatile("wbinvd" : : : "memory");
}