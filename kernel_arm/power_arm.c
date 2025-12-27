// kernel_arm/power_arm.c
// ARM 電源管理雛形
// 2025/12/27 新規作成

#include "../btron-pc/include_arm/types_arm.h"
#include <stdint.h>

// 電源管理初期化
void power_init(void) {
    // 電源管理レジスタ初期化雛形
    // 例: pmu_init();
    // SoC/ボード依存の初期化処理（QEMU/実ボード等）
    // 例: board_power_init();
}

// シャットダウン
void power_shutdown(void) {
    // シャットダウン処理雛形（SoC依存）
    // 例: ARM: WFI命令, レジスタ書き込み等
    // asm volatile ("wfi");
}

// リブート
void power_reboot(void) {
    // リブート処理雛形（SoC依存）
    // 例: ウォッチドッグタイマリセット等
    // watchdog_reset();
}

// サスペンド
void power_suspend(void) {
    // サスペンド処理雛形
    // 例: クロックゲーティング、WFI命令等
    // clock_gate();
    // asm volatile ("wfi");
}

// レジューム
void power_resume(void) {
    // レジューム処理雛形
    // 例: サスペンド解除後の復帰処理
    // clock_ungate();
}
