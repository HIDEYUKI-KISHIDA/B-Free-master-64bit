#include "interrupt.h"


// 必要な割り込みハンドラや制御関数はここで実装、または本体で実装される
void int33_handler(void) {}
// reset_intr_maskはcintrrupt.cで実装
int wait_int(int *flag) { return 0; }
void int38_handler(void) {}

// --- 未定義シンボルのダミー実装群 ---
void busywait(void) {}
void lock(void) {}
void unlock(void) {}
void intr_fd(void) {}
void intr_ide(void) {}
int kb_buffer_is_empty(void) { return 1; }
int kb_buffer_get(void) { return 0; }
void *memset(void *s, int c, unsigned int n) { char *p = s; for (unsigned int i = 0; i < n; ++i) p[i] = (char)c; return s; }
int _main(void) { return 0; }
int __main(void) { return 0; }
void ignore_handler(void) __attribute__((used));
void ignore_handler(void) {}

void _ignore_handler(void) __attribute__((used, alias("ignore_handler")));
