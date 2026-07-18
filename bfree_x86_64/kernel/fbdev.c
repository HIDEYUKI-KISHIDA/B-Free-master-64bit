#include <stdint.h>
#include <stddef.h>


// フレームバッファ情報構造体（QPA/QtWaylandからも参照可能に）
typedef struct framebuffer_t {
    uint8_t* addr;
    uint32_t width, height, pitch, bpp;
    size_t size;
    int ready;
} framebuffer_t;

static framebuffer_t g_fb_info;

// フレームバッファ情報取得API（ユーザー空間/QPA向け）
const framebuffer_t* gui_get_framebuffer_info(void) {
    return &g_fb_info;
}

// flush API（必要ならioctlやwrite経由で呼ばれる想定）
void framebuffer_flush(void) {
    // VBlank同期やDMA転送等を将来拡張
    // 今は何もしない
}
// B-Free x86_64 kernel: Minimal VBE/GOP抽象化 & /dev/fb0 デバイス雛形
// ファイル: fbdev.c
// 目的: VRAM公開・mmap対応のフレームバッファデバイス


#include <stdint.h>
#include <stddef.h>
#include "fbdev.h"
#include "device.h"
#include "gpu_backend.h"
#include "vbe_gop.h"   // struct vbe_info の正式定義

static struct vbe_info g_vbe_info;

// /dev/fb0 open
int fbdev_open(void *dev, int mode) {
    (void)dev;
    (void)mode;
    // 必要なら初期化処理
    return 0;
}

// /dev/fb0 read/write（今回は未実装、必要なら追加）


// /dev/fb0 mmap
void* fbdev_mmap(void *dev, size_t offset, size_t length) {
    (void)dev;
    if (offset + length > g_vbe_info.vram_size) return NULL;
    return (void*)(g_vbe_info.vram_phys + offset);
}

// デバイス操作関数テーブル
// --- /dev/fb0 操作関数雛形 ---
int fbdev_close(void *dev) {
    (void)dev;
    // 必要ならリソース解放
    return 0;
}
ssize_t fbdev_read(void *dev, void *buf, size_t len) {
    (void)dev;
    (void)buf;
    (void)len;
    // フレームバッファの内容をbufにコピー（例: mmap推奨のため通常は未実装）
    return 0;
}
ssize_t fbdev_write(void *dev, const void *buf, size_t len) {
    (void)dev;
    (void)buf;
    (void)len;
    // bufの内容をフレームバッファに書き込む（例: mmap推奨のため通常は未実装）
    return 0;
}
void fbdev_refresh_backend_info(void) {
    tk2gpu_fbinfo_t info;

    if (gpu_backend_ioctl(TK2GPU_IOCTL_GET_INFO, &info) != 0) {
        return;
    }

    g_vbe_info.vram_phys = (uintptr_t)info.phys_addr;
    g_vbe_info.vram_size = (size_t)info.size;
    g_vbe_info.width = info.width;
    g_vbe_info.height = info.height;
    g_vbe_info.pitch = info.pitch;
    g_vbe_info.bpp = info.bpp;

    g_fb_info.addr = (uint8_t *)(uintptr_t)info.phys_addr;
    g_fb_info.width = info.width;
    g_fb_info.height = info.height;
    g_fb_info.pitch = info.pitch;
    g_fb_info.bpp = info.bpp;
    g_fb_info.size = (size_t)info.size;
    g_fb_info.ready = 1;
}

int fbdev_ioctl(void *dev, int cmd, void *arg) {
    (void)dev;
    if (!g_fb_info.ready) {
        fbdev_init();
    }
    return gpu_backend_ioctl(cmd, arg);
}

int runtime_fbdev_ioctl(int cmd, void *arg) {
    if (!g_fb_info.ready) {
        fbdev_init();
    }
    return fbdev_ioctl(0, cmd, arg);
}

void *runtime_fbdev_mmap(size_t offset, size_t length) {
    if (!g_fb_info.ready) {
        fbdev_init();
    }
    return fbdev_mmap(0, offset, length);
}

#ifndef BFREE_RUNTIME_BUILD
static struct device_ops fbdev_ops = {
    .open = fbdev_open,
    .close = fbdev_close,
    .read = fbdev_read,
    .write = fbdev_write,
    .ioctl = fbdev_ioctl,
};

// デバイス管理構造体
static device_t fbdev = {
    .name = "/dev/fb0",
    .type = DEV_TYPE_CHAR,
    .ops = &fbdev_ops,
    .priv = &g_vbe_info,
    .next = NULL,
};
#endif

// デバイス初期化（カーネル起動時に呼ぶ）
void fbdev_init(void) {
    if (!gpu_backend_state()->initialized) {
        gpu_backend_init();
    }

    fbdev_refresh_backend_info();
#ifndef BFREE_RUNTIME_BUILD
    // /dev/fb0としてデバイス登録
    register_device(&fbdev);
#endif
}

// 必要に応じてPAT/WC属性の初期化関数も追加可
#include <stdint.h>
#include <stddef.h>
