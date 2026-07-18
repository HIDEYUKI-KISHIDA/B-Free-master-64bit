#include "mouse.h"

#include <stdint.h>
#include <stddef.h>
#include "device.h"

// devfs管理テーブルを先頭で定義
#define DEVFS_MAX_ENTRIES 32
typedef struct devfs_entry {
    const char *name;
    device_t *dev;
} devfs_entry_t;
static devfs_entry_t devfs_table[DEVFS_MAX_ENTRIES];
static int devfs_count = 0;

int devfs_register(const char *name, device_t *dev);
// --- evdevデバイス構造体と生成API（雛形） ---
#include <stdint.h>
#include <stddef.h>
#include "device.h" // device_t, device_ops_t等の定義ファイル名に合わせて修正
// #include <stdlib.h> // カーネルでは使用不可
typedef struct evdev_device {
    uint32_t device_id;
    char name[32];
    device_t dev;
    device_ops_t ops;
    // 入力デバイス種別や状態管理用の追加フィールドもここに
} evdev_device_t;

// 仮: マウス専用のevdevデバイス生成例
// --- evdevデバイス操作関数雛形 ---
ssize_t evdev_read(void *dev, void *buf, size_t len) {
    // 入力イベント構造体をbufにコピー（例: event.cのキューから取得）
    // 今はmouse_readを呼ぶ仮実装
    return mouse_read(dev, buf, len);
}
int evdev_ioctl(void *dev, int cmd, void *arg) {
    // 入力デバイス情報取得や設定（未実装）
    return mouse_ioctl(dev, cmd, arg);
}
int evdev_close(void *dev) {
    (void)dev;
    // 必要ならリソース解放
    return 0;
}

// malloc/snprintfは使えないため、静的領域と固定名で仮実装
static evdev_device_t static_evdev;
device_t* create_evdev_device(uint32_t device_id) {
    evdev_device_t* evdev = &static_evdev;
    evdev->device_id = device_id;
    // 固定名（本来はitoa等で"evdev0"等を生成）
    evdev->name[0] = 'e'; evdev->name[1] = 'v'; evdev->name[2] = 'd'; evdev->name[3] = 'e'; evdev->name[4] = 'v'; evdev->name[5] = '0' + (device_id & 0xF); evdev->name[6] = '\0';
    evdev->ops.read = evdev_read;
    evdev->ops.ioctl = evdev_ioctl;
    evdev->ops.open = NULL;
    evdev->ops.close = evdev_close;
    evdev->ops.write = NULL;
    evdev->dev.name = evdev->name;
    evdev->dev.type = DEV_TYPE_CHAR;
    evdev->dev.ops = &evdev->ops;
    evdev->dev.priv = evdev;
    evdev->dev.next = NULL;
    return &evdev->dev;
}
// evdevノードの動的追加/削除API

// --- カーネル用簡易文字列関数 ---
// static void kstrncpy(char *dst, const char *src, int n) {
//     int i;
//     for (i = 0; i < n-1 && src[i]; ++i) dst[i] = src[i];
//     dst[i] = '\0';
// }
static int kstrcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return *a - *b;
        ++a; ++b;
    }
    return *a - *b;
}

void devfs_add_evdev_node(uint32_t device_id) {
    char name[32] = "/dev/input/event";
    int idx = 17;
    unsigned int n = device_id;
    if (n == 0) { name[idx++] = '0'; name[idx] = '\0'; }
    else {
        char buf[10]; int p = 0;
        while (n && p < 9) { buf[p++] = '0' + (n % 10); n /= 10; }
        while (p--) name[idx++] = buf[p];
        name[idx] = '\0';
    }
    // 仮: evdevデバイス構造体を生成し登録（本来はdevice_tを生成）
    extern device_t* create_evdev_device(uint32_t device_id);
    device_t* dev = create_evdev_device(device_id);
    devfs_register(name, dev);
}

void devfs_remove_evdev_node(uint32_t device_id) {
    char name[32] = "/dev/input/event";
    int idx = 17;
    unsigned int n = device_id;
    if (n == 0) { name[idx++] = '0'; name[idx] = '\0'; }
    else {
        char buf[10]; int p = 0;
        while (n && p < 9) { buf[p++] = '0' + (n % 10); n /= 10; }
        while (p--) name[idx++] = buf[p];
        name[idx] = '\0';
    }
    for (int i = 0; i < devfs_count; ++i) {
        if (kstrcmp(devfs_table[i].name, name) == 0) {
            devfs_table[i].name = NULL;
            devfs_table[i].dev = NULL;
            break;
        }
    }
}
// /devノード登録
int devfs_register(const char *name, device_t *dev) {
    if (devfs_count >= DEVFS_MAX_ENTRIES) return -1;
    devfs_table[devfs_count].name = name;
    devfs_table[devfs_count].dev = dev;
    devfs_count++;
    return 0;
}

// /devノード検索
static device_t *devfs_find(const char *name) {
    for (int i = 0; i < devfs_count; ++i) {
        if (devfs_table[i].name && kstrcmp(devfs_table[i].name, name) == 0)
            return devfs_table[i].dev;
    }
    return NULL;
}

// ユーザ空間API雛形
int devfs_open(const char *name, int mode) {
    device_t *dev = devfs_find(name);
    if (!dev || !dev->ops || !dev->ops->open) return -1;
    return dev->ops->open(dev, mode);
}
ssize_t devfs_read(const char *name, void *buf, size_t len) {
    device_t *dev = devfs_find(name);
    if (!dev || !dev->ops || !dev->ops->read) return -1;
    return dev->ops->read(dev, buf, len);
}
ssize_t devfs_write(const char *name, const void *buf, size_t len) {
    device_t *dev = devfs_find(name);
    if (!dev || !dev->ops || !dev->ops->write) return -1;
    return dev->ops->write(dev, buf, len);
}
int devfs_ioctl(const char *name, int cmd, void *arg) {
    device_t *dev = devfs_find(name);
    if (!dev || !dev->ops || !dev->ops->ioctl) return -1;
    return dev->ops->ioctl(dev, cmd, arg);
}
