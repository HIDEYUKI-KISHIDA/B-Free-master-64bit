// --- RTC時刻取得・設定API雛形 ---
int rtc_get_time(void *dev, struct tm *out) {
    // TODO: 現在時刻をoutに格納
    (void)dev; (void)out;
    return 0;
}

int rtc_set_time(void *dev, const struct tm *in) {
    // TODO: inの内容で時刻設定
    (void)dev; (void)in;
    return 0;
}
#include <stdint.h>
#include "device.h"

// RTC（リアルタイムクロック）デバイス雛形
// 通常はI/Oポート0x70/0x71またはACPI/SMBus経由

struct rtc_info {
    int type; // 0:CMOS, 1:ACPI, 2:Other
};


// --- RTC read/write/ioctl 雛形 ---
static int rtc_open(void *dev, int mode) {
    // 必要なら初期化
    return 0;
}
static int rtc_close(void *dev) {
    return 0;
}
static ssize_t rtc_read(void *dev, void *buf, size_t len) {
    // TODO: 現在時刻をbufに格納
    (void)dev; (void)buf; (void)len;
    return 0;
}
static ssize_t rtc_write(void *dev, const void *buf, size_t len) {
    // TODO: bufの内容で時刻設定
    (void)dev; (void)buf; (void)len;
    return 0;
}
static int rtc_ioctl(void *dev, int cmd, void *arg) {
    // 例: アラーム設定/解除、時刻フォーマット指定など
    (void)dev; (void)cmd; (void)arg;
    return 0;
}

void register_rtc_device(int type) {
    static struct rtc_info priv;
    priv.type = type;
    static struct device_ops ops = {
        .open = rtc_open,
        .close = rtc_close,
        .read = rtc_read,
        .write = rtc_write,
        .ioctl = rtc_ioctl
    };
    static device_t rtc_dev = {
        .name = "rtc0",
        .type = DEV_TYPE_OTHER,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&rtc_dev);
}
