// ARM用ユーザランドサンプル
#include <stdio.h>
extern void uart_init(void);
extern void uart_putc(char c);
extern void timer_init(void);
extern unsigned int timer_get_count(void);
extern void gpio_init(void);
extern void gpio_set(int pin, int value);
extern int gpio_get(int pin);

void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

int main() {
	uart_init();
	timer_init();
	gpio_init();

	uart_puts("Hello ARM!\n");

	// タイマ値取得例
	unsigned int t = timer_get_count();
	char buf[32];
	sprintf(buf, "Timer: %u\n", t);
	uart_puts(buf);

	// GPIO操作例
	gpio_set(17, 1); // 例: GPIO17をHigh
	int v = gpio_get(17);
	sprintf(buf, "GPIO17: %d\n", v);
	uart_puts(buf);

	return 0;
}
