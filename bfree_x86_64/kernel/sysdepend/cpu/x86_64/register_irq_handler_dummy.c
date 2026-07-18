// 割り込みハンドラ配列方式の本実装
#include <stdint.h>

typedef void (*irq_handler_t)(void *regs);

// 256個のベクタに対応するハンドラ関数ポインタの配列
irq_handler_t irq_handlers[256] = {0};

// 本実装：ハンドラを配列に登録する
void register_irq_handler(int vecno, irq_handler_t handler) {
	if (vecno >= 0 && vecno < 256) {
		irq_handlers[vecno] = handler;
	}
}
