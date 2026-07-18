#include <stdint.h>

// 電源管理API雛形

typedef enum {
    POWER_STATE_ON,
    POWER_STATE_SLEEP,
    POWER_STATE_OFF,
    POWER_STATE_REBOOT
} power_state_t;

static power_state_t current_power_state = POWER_STATE_ON;

// 電源状態変更
void power_set_state(power_state_t state) {
    // TODO: ACPI/ハードウェア連携
    current_power_state = state;
}

// 現在の電源状態取得
power_state_t power_get_state(void) {
    return current_power_state;
}
