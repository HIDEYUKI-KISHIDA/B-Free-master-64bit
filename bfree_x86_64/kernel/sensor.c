// --- センサ値取得・イベントAPI雛形 ---
int sensor_get_value(void *dev, void *out, size_t outlen) {
    // TODO: センサ値をoutに格納
    (void)dev; (void)out; (void)outlen;
    return 0;
}

int sensor_register_event(void *dev, int event_type, void (*callback)(void *)) {
    // TODO: 指定イベント発生時にcallbackを呼ぶ
    (void)dev; (void)event_type; (void)callback;
    return 0;
}
#include <stdint.h>
#include "device.h"

// 各種センサ（温度・加速度・照度・近接・気圧・地磁気など）デバイス雛形
// ACPI/SMBus/I2C/PCI等で検出・登録可能

struct sensor_info {
    int type; // 0:温度, 1:加速度, 2:照度, 3:近接, 4:気圧, 5:地磁気, 6:その他
};


// --- センサ read/write/ioctl 雛形 ---
static int sensor_open(void *dev, int mode) {
    // 必要なら初期化
    return 0;
}
static int sensor_close(void *dev) {
    return 0;
}
static ssize_t sensor_read(void *dev, void *buf, size_t len) {
    // TODO: センサ値をbufに格納
    (void)dev; (void)buf; (void)len;
    return 0;
}
static ssize_t sensor_write(void *dev, const void *buf, size_t len) {
    // TODO: センサ制御コマンド送信等
    (void)dev; (void)buf; (void)len;
    return 0;
}
static int sensor_ioctl(void *dev, int cmd, void *arg) {
    // 例: 測定モード切替/キャリブレーション等
    (void)dev; (void)cmd; (void)arg;
    return 0;
}

void register_sensor_device(int type) {
    static struct sensor_info priv;
    priv.type = type;
    static struct device_ops ops = {
        .open = sensor_open,
        .close = sensor_close,
        .read = sensor_read,
        .write = sensor_write,
        .ioctl = sensor_ioctl
    };
    static device_t sensor_dev = {
        .name = "sensor0",
        .type = DEV_TYPE_OTHER,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&sensor_dev);
}
