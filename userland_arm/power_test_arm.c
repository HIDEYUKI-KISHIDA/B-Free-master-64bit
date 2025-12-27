// userland_arm/power_test_arm.c
// 電源管理動作テスト用サンプル
// 2025/12/27 新規作成

#include <stdio.h>

extern void power_init(void);
extern void power_shutdown(void);
extern void power_reboot(void);
extern void power_suspend(void);
extern void power_resume(void);

int main(void) {
    printf("電源管理テスト開始\n");
    power_init();
    // 実際のシャットダウン・リブート・サスペンドはコメントアウト例
    // power_shutdown();
    // power_reboot();
    // power_suspend();
    // power_resume();
    printf("電源管理テスト終了\n");
    return 0;
}
