// userland_arm/device_test_arm.c
// ARM/64ビット用デバイスドライバ異常系・エラーケース自動テスト
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdio.h>

// NICドライバAPI（雛形）
extern int nic_init_arm(void);
extern int nic_send_arm(const void *data, uint16_t len);
extern int nic_recv_arm(void *buf, uint16_t buflen);

void test_nic_error_cases() {
    printf("[TEST] NIC異常系テスト\n");
    int ret = nic_send_arm(NULL, 100);
    printf("  nic_send_arm(NULL): %s\n", ret<0?"NG":"OK(要実装)\n");
    char buf[8];
    ret = nic_recv_arm(NULL, 8);
    printf("  nic_recv_arm(NULL): %s\n", ret<0?"NG":"OK(要実装)\n");
    ret = nic_recv_arm(buf, 0);
    printf("  nic_recv_arm(0): %s\n", ret<0?"NG":"OK(要実装)\n");
}

int main(void) {
    printf("デバイスドライバ異常系テスト開始\n");
    test_nic_error_cases();
    printf("デバイスドライバ異常系テスト終了\n");
    return 0;
}
