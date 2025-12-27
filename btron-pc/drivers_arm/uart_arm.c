
#define UART_BASE 0x3F201000 // 例: Raspberry Pi 3 PL011 UART

void uart_init(void) {
	// UART初期化処理（レジスタ設定等）
}

void uart_putc(char c) {
	// 送信レジスタ空き待ち・送信（ダミー）
	(void)c;
}

char uart_getc(void) {
	// 受信レジスタから1文字取得（ダミー）
	return 0;
}
