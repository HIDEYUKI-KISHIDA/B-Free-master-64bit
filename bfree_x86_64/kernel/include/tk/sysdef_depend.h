/*
 * sysdef_depend.h - x86_64 T-Kernel2.0 用依存型定義
 * 仕様: TK2_x86_64_Spec.md 2.3節準拠
 */
#ifndef __TK_SYSDEF_DEPEND_H__
#define __TK_SYSDEF_DEPEND_H__

#include <stdint.h>

/*
 * 割込み/例外時のレジスタ保存構造体
 * pushq順序の逆順で定義（スタックレイアウトと一致）
 * 合計160バイト（16バイトアライン）
 */
typedef struct {
    /* Low Address: 最後にpushqされた順 */
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rbp;
    /* ハードウェアフレーム: CPUが自動でpushする領域 */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} T_REGS;

#endif /* __TK_SYSDEF_DEPEND_H__ */
