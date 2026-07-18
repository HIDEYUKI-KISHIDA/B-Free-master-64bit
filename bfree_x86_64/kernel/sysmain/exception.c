// exception.c - 割り込み・例外ハンドラ（仕様書v2.4準拠）
//
// * T_REGS構造体（pushq逆順）
// * GAS構文エントリ例は exception.S 参照
// * ページフォルト例外ハンドラ（CR2取得・errcode処理）

#include <stdint.h>

// T_REGS: pushq逆順（仕様書v2.4）
typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rbp;
    uint64_t rip, cs, rflags, rsp, ss;
} T_REGS;

// ページフォルト例外ハンドラ
void page_fault_handler(T_REGS* regs, uint64_t errcode) {
    (void)regs;
    (void)errcode;
    uint64_t cr2;
    asm volatile ("mov %%cr2, %0" : "=r"(cr2));
    // printf("[EXCEPTION] Page Fault! CR2(addr)=%016llx errcode=0x%llx\n", cr2, errcode); // 標準Cライブラリ不可
    // 必要ならシリアル出力等に置換
    // 動的ページ割当雛形（必要に応じて本実装）
    // ...
    // 致命的例外時は停止
    while (1) { asm volatile ("cli; hlt"); }
}

// 他の例外ハンドラも同様に追加可能
