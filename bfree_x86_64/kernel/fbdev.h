// B-Free x86_64 kernel: /dev/fb0デバイス登録用
// ファイル: fbdev.h
#ifndef FBDEV_H
#define FBDEV_H

#include <stddef.h>

void fbdev_init(void);
void fbdev_refresh_backend_info(void);
int runtime_fbdev_ioctl(int cmd, void *arg);
void *runtime_fbdev_mmap(size_t offset, size_t length);

#endif // FBDEV_H
