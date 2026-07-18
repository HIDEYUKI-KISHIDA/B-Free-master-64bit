
#include <stdarg.h>
extern void uart_puts(const char *);

// 最小限のsyslog実装（本格実装は後で拡張）
void syslog(int priority, const char *format, ...) {
    (void)priority;
    // 可変引数は未処理。まずはformat文字列をそのまま出力
    uart_puts("[SYSLOG] ");
    uart_puts(format);
    uart_puts("\n");
}
