#include <stdint.h>
#include "power.c"

// ACPI連携による電源管理高度化雛形

// ACPIテーブルからの状態取得（ダミー）
int acpi_get_power_state(void) {
    // TODO: 実際はACPIテーブルをパース
    return POWER_STATE_ON;
}

// ACPI経由で電源状態変更（ダミー）
int acpi_set_power_state(int state) {
    // TODO: 実際はACPI制御
    power_set_state(state);
    return 0;
}

// 今後: S3/S4/S5等のサスペンド・レジューム・シャットダウン制御拡張
