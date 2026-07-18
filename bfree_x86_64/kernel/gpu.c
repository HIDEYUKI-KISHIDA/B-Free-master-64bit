#ifdef __cplusplus
extern "C" {
#endif
void serial_puts(const char *s);
#ifdef __cplusplus
}
#endif
// --- グラフィック描画API雛形 ---
#include <stddef.h>
extern int framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern int framebuffer_init(uint8_t* addr, uint32_t pitch, uint32_t width, uint32_t height, uint8_t bpp);
// framebuffer_tと情報取得APIをfbdev.cから利用
#include "fbdev.c" // または適切なヘッダに変更
extern const framebuffer_t* gui_get_framebuffer_info(void);
extern void framebuffer_flush(void);


// QPA/QtWaylandからの高速描画用: ユーザー空間がmmapでバッファを直接書き換え、必要に応じてflushを呼ぶ
// --- パフォーマンス最適化: DMA/VBlank同期/PAT/WC属性 ---
#include <stdbool.h>
static bool g_use_dma = false;
static bool g_use_vblank = false;

/* x86_64 PAT (Page Attribute Table) を用いた Write-Combining の有効化 */
void framebuffer_set_pat_wc(void* addr, size_t size) {
    uint32_t low, high;
    /* IA32_PAT MSR (0x277) を読み出し、PAT4 エントリを WC (01h) に設定 */
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x277));
    
    /* PAT4 は 64bit MSR の bits 32-34 (high の bits 0-2) */
    high = (high & ~0x07) | 0x01; 
    
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0x277));
    serial_puts("[SERIAL][GPU] PAT Write-Combining enabled for VRAM\n");
    
    (void)addr; (void)size;
    __asm__ volatile("sfence" ::: "memory");
}

// VBlank同期（仮実装）
void wait_vblank(void) {
    if (g_use_vblank) {
        // MMIO/割り込み等でVBlank待ち（実装はGPU依存）
    }
}

// DMA転送（仮実装）
void dma_fill_rect(uint8_t* fb_addr, int fb_pitch, int x, int y, int w, int h, uint32_t color) {
    // 実際はDMAエンジンに命令発行（GPU依存）
    // ここではmemset/memcpyの高速化例のみ
    for (int j = 0; j < h; ++j) {
        uint32_t* row = (uint32_t*)(fb_addr + (y + j) * fb_pitch + x * 4);
        for (int i = 0; i < w; ++i) row[i] = color;
    }
}

int gpu_fill_rect(void *dev, int x, int y, int w, int h, uint32_t color) {
    const framebuffer_t* fb = gui_get_framebuffer_info();
    if (!fb || !fb->ready) return -1;
    framebuffer_set_pat_wc(fb->addr, fb->size); // WC属性有効化
    if (g_use_dma) {
        dma_fill_rect(fb->addr, fb->pitch, x, y, w, h, color);
    } else {
        uint32_t stride = fb->pitch / 4;
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) {
                int px = x + i, py = y + j;
                if (px >= 0 && px < (int)fb->width && py >= 0 && py < (int)fb->height) {
                    ((uint32_t*)fb->addr)[py * stride + px] = color;
                }
            }
        }
    }
    wait_vblank();
    framebuffer_flush();
    return 0;
}

// --- OpenGL ES/EGL対応のための基礎構造体/API雛形 ---
typedef struct {
    void* egl_display;
    void* egl_context;
    void* egl_surface;
    // ...
} gpu_egl_context_t;

int gpu_init_egl(gpu_egl_context_t* ctx) {
    // EGLDisplay/EGLContext/EGLSurfaceの初期化（実装はOpenGL ES/EGLライブラリ依存）
    serial_puts("[SERIAL][GPU] gpu_init_egl called\n");
    (void)ctx;
    return 0;
}

int gpu_clear_screen(void *dev, uint32_t color) {
    // 画面全体をcolorでクリア
    serial_puts("[SERIAL][GPU] gpu_clear_screen called\n");
    const framebuffer_t* fb = gui_get_framebuffer_info();
    if (fb && fb->ready) {
        framebuffer_fill_rect(0, 0, fb->width, fb->height, color);
    }
    return 0;
}
#include <stdint.h>
#include "device.h"


// --- PCIスキャン・MMIO初期化拡張 ---
// --- GPU対応状況の拡張: PCIスキャン・MMIO初期化強化・将来のGPU対応設計 ---
#include <stdio.h>
typedef struct {
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if;
    uint32_t bar[6];
    int irq;
} pci_gpu_info_t;

int enumerate_gpus(pci_gpu_info_t* out, int max_gpus) {
    // PCIバス全体をスキャンし、GPU候補を列挙（本来はPCI config空間を読む）
    // ここではclass_code=0x03のみ例示
    extern int pci_scan_class_multi(uint8_t class_code, uint8_t subclass, uint8_t *bus, uint8_t *dev, uint8_t *func, uint32_t *bar, int max_count);
    uint8_t bus[8], dev[8], func[8];
    uint32_t bar[8];
    int count = pci_scan_class_multi(0x03, 0x00, bus, dev, func, bar, max_gpus);
    for (int i = 0; i < count; ++i) {
        out[i].vendor_id = 0; // PCI configから取得
        out[i].device_id = 0;
        out[i].class_code = 0x03;
        out[i].subclass = 0x00;
        out[i].prog_if = 0;
        for (int j = 0; j < 6; ++j) out[i].bar[j] = 0; // PCI configから取得
        out[i].bar[0] = bar[i];
        out[i].irq = -1;
    }
    return count;
}

void gpu_pci_scan_and_init(void) {
    pci_gpu_info_t gpus[8];
    int count = enumerate_gpus(gpus, 8);
    for (int i = 0; i < count; ++i) {
        volatile void *mmio_base = (volatile void *)(uintptr_t)(gpus[i].bar[0] & ~0xF);
        register_gpu_device(mmio_base, gpus[i].vendor_id, gpus[i].device_id, 0);
        struct gpu_info gpu;
        gpu.mmio_base = mmio_base;
        gpu.vendor_id = gpus[i].vendor_id;
        gpu.device_id = gpus[i].device_id;
        gpu.type = 0;
        gpu_framebuffer_init(&gpu);
        // 将来的なGPU対応: NVIDIA/AMD/Intel等はここでベンダID/デバイスIDで分岐し、専用初期化を呼ぶ
    }
}

// --- フレームバッファ管理の拡張 ---
typedef struct {
    void* mmio_base;
    uint32_t fb_size;
    uint32_t caps; // 機能ビット: DMA, VBlank, OpenGL等
    // ...
} gpu_fb_info_t;

void gpu_query_fb_info(struct gpu_info* gpu, gpu_fb_info_t* out) {
    // MMIO経由でフレームバッファ情報や機能を取得（GPU依存）
    if (!gpu || !out) return;
    out->mmio_base = gpu->mmio_base;
    out->fb_size = 8 * 1024 * 1024; // 仮
    out->caps = 0x7; // 例: DMA/VBlank/OpenGLサポート
}

// --- QPA/Wayland拡張プロトコル・C++/QMLバインディング設計例（コメント） ---
/*
// QPA: QPlatformIntegrationBFree::nativeEventFilterでTK2独自イベントを受信
// Wayland: 拡張プロトコル（wl_tk2_ipc等）を定義し、C++/QMLからバインディング
// 例: QMLからTK2システムイベント送信
// tk2.sendSystemEvent("power_suspend", {...})
*/

// --- フレームバッファ初期化・管理雛形 ---
// DRM/KMS的な抽象化構造体（雛形）
struct drm_mode {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint64_t fb_addr;
};

void gpu_framebuffer_init(struct gpu_info *gpu) {
    // MMIO経由でフレームバッファアドレス・解像度等を取得し初期化（雛形）
    if (!gpu || !gpu->mmio_base) return;
    struct drm_mode mode;
    // ここでMMIOから値を取得する処理を実装（例: レジスタ読み出し）
    // mode.width = ...; mode.height = ...; mode.bpp = ...; mode.pitch = ...; mode.fb_addr = ...;
    // 仮の値
    mode.width = 1024;
    mode.height = 768;
    mode.bpp = 32;
    mode.pitch = 1024 * 4;
    mode.fb_addr = (uint64_t)gpu->mmio_base;
    framebuffer_init((uint8_t*)mode.fb_addr, mode.pitch, mode.width, mode.height, mode.bpp);
}

// --- 割り込みハンドラ雛形 ---
void gpu_irq_handler(void *regs) {
    (void)regs;
    // TODO: VBlank/描画完了/エラー等の割り込み処理
}

struct gpu_info {
    volatile void *mmio_base;
    uint16_t vendor_id, device_id;
    int type; // 0:VGA, 1:GPU, 2:Other
};


// --- グラフィック read/write/ioctl 雛形 ---
static int gpu_open(void *dev, int mode) {
    // 必要なら初期化
    return 0;
}
static int gpu_close(void *dev) {
    return 0;
}
static ssize_t gpu_read(void *dev, void *buf, size_t len) {
    // TODO: フレームバッファやVGAレジスタ等から読み出し
    (void)dev; (void)buf; (void)len;
    return 0;
}
static ssize_t gpu_write(void *dev, const void *buf, size_t len) {
    // ユーザー空間からの描画命令: bufをフレームバッファに書き込む
    // 例: RGB32ピクセル配列を左上から順に描画
    const framebuffer_t* fb = gui_get_framebuffer_info();
    if (!fb || !fb->ready) return -1;
    size_t px_n = len / 4;
    uint32_t *dst = (uint32_t*)fb->addr;
    const uint32_t *src = (const uint32_t*)buf;
    if (px_n > fb->width * fb->height) px_n = fb->width * fb->height;
    for (size_t i = 0; i < px_n; ++i) dst[i] = src[i];
    return px_n * 4;
}
static int gpu_ioctl(void *dev, int cmd, void *arg) {
    // 例: 解像度変更/VSync制御/EDID取得など
    (void)dev; (void)cmd; (void)arg;
    return 0;
}

void register_gpu_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type) {
    static struct gpu_info priv;
    priv.mmio_base = mmio_base;
    priv.vendor_id = vendor_id;
    priv.device_id = device_id;
    priv.type = type;
    static struct device_ops ops = {
        .open = gpu_open,
        .close = gpu_close,
        .read = gpu_read,
        .write = gpu_write,
        .ioctl = gpu_ioctl
    };
    static device_t gpu_dev = {
        .name = "gpu0",
        .type = DEV_TYPE_OTHER,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&gpu_dev);
    // devfsにノード登録
    extern int devfs_register(const char *name, device_t *dev);
    devfs_register("gpu0", &gpu_dev);
    // 割り込みハンドラ登録（例: IRQはGPUごとに異なる場合あり）
    // extern void register_irq_handler(int irq, void* handler);
    // register_irq_handler(..., gpu_irq_handler);
}
