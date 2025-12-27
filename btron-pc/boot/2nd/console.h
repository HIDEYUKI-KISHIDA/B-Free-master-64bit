#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#define MAX_HEIGHT 25
#define MAX_WIDTH 80
// 必要に応じてダミー関数や型を追加
void console_clear(void);
// static inline void console_init(void) {}
static inline void console_putc(char c) {}
static inline void console_puts(const char *s) {}

#endif
