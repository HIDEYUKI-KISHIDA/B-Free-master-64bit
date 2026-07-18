/*
 * g_fpu_save_mode - XSAVE/XSAVEOPT/FXSAVE自動切替用グローバル変数
 * 0=FXSAVE, 1=XSAVE, 2=XSAVEOPT
 */
#include <stdint.h>

int g_fpu_save_mode = 0; // デフォルト: FXSAVE
uint64_t g_fpu_rfbm = 0x7; // x87+SSE+AVX

/* Enable SSE/x87 for ring3 (Qt guest uses movq/xmm immediately).
 * Without CR4.OSFXSR, SSE opcodes raise #UD in user mode. */
void bfree_enable_user_fpu(void)
{
    uint64_t cr0;
    uint64_t cr4;

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10); /* OSFXSR | OSXMMEXCPT */
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");

    __asm__ volatile("fninit");

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((1ULL << 3) | (1ULL << 2)); /* clear TS, EM */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
}
