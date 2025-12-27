// userland_arm/peripheral_test_arm.c
// ARM/64ビット 周辺API/デバイス自動テスト雛形
// 2025/12/27 新規作成

#include <stdio.h>
#include <stdint.h>

// テスト対象API（雛形）
extern int fs_open(const char *name);
extern int fs_read(int fd, void *buf, uint32_t size);
extern int fs_write(int fd, const void *buf, uint32_t size);
extern int fs_close(int fd);
extern void uart_init(void);
extern void uart_putc(char c);
extern char uart_getc(void);
extern void power_shutdown(void);
extern void power_reboot(void);
extern int icmp_send_arm(uint32_t dst_ip, uint8_t type, uint8_t code, const void *data, uint16_t len);

void test_fs_api() {
    printf("[TEST] ファイルシステムAPI\n");
    int fd = fs_open("test.txt");
    printf("  fs_open: fd=%d\n", fd);
    char buf[16];
    int r = fs_read(fd, buf, sizeof(buf));
    printf("  fs_read: %d\n", r);
    r = fs_write(fd, "hello", 5);
    printf("  fs_write: %d\n", r);
    r = fs_close(fd);
    printf("  fs_close: %d\n", r);
}

void test_uart_api() {
    printf("[TEST] UART API\n");
    uart_init();
    uart_putc('A');
    char c = uart_getc();
    printf("  uart_getc: %c\n", c);
}

void test_power_api() {
    printf("[TEST] 電源管理API\n");
    // power_shutdown(); // コメントアウト: 実行注意
    // power_reboot();   // コメントアウト: 実行注意
}

void test_icmp_api() {
    printf("[TEST] ICMP API\n");
    int r = icmp_send_arm(0xC0A80002, 8, 0, NULL, 0); // Echo Request
    printf("  icmp_send_arm: %d\n", r);
}

int main(void) {
    test_fs_api();
    test_uart_api();
    test_power_api();
    test_icmp_api();
    return 0;
}
