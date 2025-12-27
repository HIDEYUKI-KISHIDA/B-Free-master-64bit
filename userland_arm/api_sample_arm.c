// userland_arm/api_sample_arm.c
// ARM/64ビット用ユーザーランドAPI利用サンプル拡充
// 2025/12/27 新規作成

#include "../include_arm/net_api_arm.h"
#include "../include_arm/debug_arm.h"
#include <stdio.h>
#include <string.h>

void sample_udp_send() {
    printf("[SAMPLE] UDP送信\n");
    uint8_t data[8] = "HELLOARM";
    int ret = udp_send_arm(0xC0A80002, 2000, data, 8);
    printf("  udp_send_arm: %s\n", ret==0?"OK":"NG");
}

void sample_debug_log() {
    printf("[SAMPLE] デバッグログAPI\n");
    debug_set_level(DBG_LEVEL_DEBUG);
    debug_log_level(DBG_LEVEL_INFO, "info log: %d", 123);
    debug_log_level(DBG_LEVEL_DEBUG, "debug log: %s", "test");
}

int main(void) {
    printf("ユーザーランドAPIサンプル開始\n");
    sample_udp_send();
    sample_debug_log();

    // --- プロセスAPI利用例 ---
    printf("[SAMPLE] プロセスAPI\n");
    // process_init();
    // int pid = process_create(NULL);
    // schedule();

    // --- ファイルシステムAPI利用例 ---
    printf("[SAMPLE] ファイルAPI\n");
    // fs_init();
    // int fd = fs_open("test.txt");
    // char buf[64];
    // fs_read(fd, buf, sizeof(buf));
    // fs_write(fd, "hello", 5);
    // fs_close(fd);

    // --- 権限管理API利用例 ---
    printf("[SAMPLE] 権限管理API\n");
    // security_init();
    // int uid = user_add("user", 1);
    // int ok = check_access(uid, 0, 4);

    // --- 電源管理API利用例 ---
    printf("[SAMPLE] 電源管理API\n");
    // power_init();
    // power_shutdown();

    // --- UART/TTY API利用例 ---
    printf("[SAMPLE] UART/TTY API\n");
    // uart_init();
    // uart_putc('A');
    // char c = uart_getc();
    // tty_write("hello\n");

    // --- シェルAPI利用例 ---
    printf("[SAMPLE] シェルAPI\n");
    // shell_main();

    printf("ユーザーランドAPIサンプル終了\n");
    return 0;
}
