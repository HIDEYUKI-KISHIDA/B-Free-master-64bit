// evdevノードの動的追加/削除API（ディスパッチ層）
#include <stdio.h>
char evdev_dispatch_names[DEVFS_DISPATCH_MAX][32];

void devfs_dispatch_add_evdev_node(uint32_t device_id, int (*open)(void), int (*read)(void*,size_t), int (*write)(const void*,size_t), int (*ioctl)(int,void*)) {
    char name[32];
    snprintf(name, sizeof(name), "/dev/input/event%u", device_id);
    strncpy(evdev_dispatch_names[devfs_dispatch_count], name, sizeof(evdev_dispatch_names[devfs_dispatch_count])-1);
    evdev_dispatch_names[devfs_dispatch_count][sizeof(evdev_dispatch_names[devfs_dispatch_count])-1] = '\0';
    devfs_dispatch_register(evdev_dispatch_names[devfs_dispatch_count], open, read, write, ioctl);
}

void devfs_dispatch_remove_evdev_node(uint32_t device_id) {
    char name[32];
    snprintf(name, sizeof(name), "/dev/input/event%u", device_id);
    for (int i = 0; i < devfs_dispatch_count; ++i) {
        if (strcmp(devfs_dispatch_table[i].name, name) == 0) {
            devfs_dispatch_table[i].name = "";
            devfs_dispatch_table[i].open = NULL;
            devfs_dispatch_table[i].read = NULL;
            devfs_dispatch_table[i].write = NULL;
            devfs_dispatch_table[i].ioctl = NULL;
            break;
        }
    }
}
#include <string.h>
#include <stddef.h>
#include "devfs.c"

// /dev仮想デバイスノードのユーザー空間APIディスパッチ層雛形

typedef struct {
    const char *name;
    int (*open)(void);
    int (*read)(void *buf, size_t len);
    int (*write)(const void *buf, size_t len);
    int (*ioctl)(int cmd, void *arg);
} devfs_dispatch_entry_t;

#define DEVFS_DISPATCH_MAX 64
static devfs_dispatch_entry_t devfs_dispatch_table[DEVFS_DISPATCH_MAX];
static int devfs_dispatch_count = 0;

// デバイスノード登録
int devfs_dispatch_register(const char *name, int (*open)(void), int (*read)(void*,size_t), int (*write)(const void*,size_t), int (*ioctl)(int,void*)) {
    if (devfs_dispatch_count >= DEVFS_DISPATCH_MAX) return -1;
    devfs_dispatch_table[devfs_dispatch_count].name = name;
    devfs_dispatch_table[devfs_dispatch_count].open = open;
    devfs_dispatch_table[devfs_dispatch_count].read = read;
    devfs_dispatch_table[devfs_dispatch_count].write = write;
    devfs_dispatch_table[devfs_dispatch_count].ioctl = ioctl;
    devfs_dispatch_count++;
    return 0;
}

// open/read/write/ioctlディスパッチ
int devfs_dispatch_open(const char *name) {
    for (int i = 0; i < devfs_dispatch_count; ++i) {
        if (strcmp(devfs_dispatch_table[i].name, name) == 0 && devfs_dispatch_table[i].open)
            return devfs_dispatch_table[i].open();
    }
    return -1;
}
int devfs_dispatch_read(const char *name, void *buf, size_t len) {
    for (int i = 0; i < devfs_dispatch_count; ++i) {
        if (strcmp(devfs_dispatch_table[i].name, name) == 0 && devfs_dispatch_table[i].read)
            return devfs_dispatch_table[i].read(buf, len);
    }
    return -1;
}
int devfs_dispatch_write(const char *name, const void *buf, size_t len) {
    for (int i = 0; i < devfs_dispatch_count; ++i) {
        if (strcmp(devfs_dispatch_table[i].name, name) == 0 && devfs_dispatch_table[i].write)
            return devfs_dispatch_table[i].write(buf, len);
    }
    return -1;
}
int devfs_dispatch_ioctl(const char *name, int cmd, void *arg) {
    for (int i = 0; i < devfs_dispatch_count; ++i) {
        if (strcmp(devfs_dispatch_table[i].name, name) == 0 && devfs_dispatch_table[i].ioctl)
            return devfs_dispatch_table[i].ioctl(cmd, arg);
    }
    return -1;
}
