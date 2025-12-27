// userland_arm/irq_test_arm.c
// 割り込みコントローラ・ハンドラ動作テスト用サンプル
// 2025/12/27 新規作成

#include "../../btron-pc/include_arm/types_arm.h"
#include <stdio.h>

extern void gic_init(void);
extern void register_interrupt_handler(int irq, void (*handler)(void));
extern void enable_interrupts(void);

void test_irq_handler(void) {
    printf("IRQ発生: test_irq_handlerが呼ばれました\n");
}

int main(void) {
    printf("IRQテスト開始\n");
    gic_init();
    register_interrupt_handler(5, test_irq_handler); // IRQ5にハンドラ登録
    enable_interrupts();
    // 実際の割り込み発生はハード依存
    printf("IRQテスト終了\n");
    return 0;
}
