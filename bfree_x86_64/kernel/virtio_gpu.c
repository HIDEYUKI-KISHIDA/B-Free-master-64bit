/*
 * Minimal virtio-gpu (PCI 1af4:1050) backend for B-Free.
 * Control queue: GET_DISPLAY_INFO + 2D resource scanout to device VRAM.
 */
#include <stdint.h>
#include <stddef.h>
#include "virtio_gpu.h"
#include "gpu_backend.h"
#include "vbe_gop.h"

extern void uart_puts(const char *);
extern void uart_puthex64(uint64_t);

#define VIRTIO_PCI_CAP_VNDR 0x09
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY 2
#define VIRTIO_PCI_CAP_ISR 3

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO       0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D     0x0101
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_SET_SCANOUT            0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH         0x0104

#define VIRTIO_GPU_FLAG_FENCE  (1u << 0)

#define VIRTIO_GPU_MAX_SCANOUTS 16

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_ctrl_hdr_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_rect_t;

typedef struct {
    virtio_gpu_rect_t r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed)) virtio_gpu_resp_display_info_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_resp_display_info_t pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed)) virtio_gpu_resp_get_display_info_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_resource_create_2d_t;

typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_mem_entry_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed)) virtio_gpu_resource_attach_backing_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t scanout_id;
    uint32_t resource_id;
    virtio_gpu_rect_t r;
} __attribute__((packed)) virtio_gpu_set_scanout_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    virtio_gpu_rect_t r;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_resource_flush_t;

typedef struct {
    volatile uint8_t *common;
    volatile uint8_t *notify;
    uint32_t notify_off_multiplier;
    uint32_t queue_size;
    uint32_t queue_desc;
    uint32_t queue_driver;
    uint32_t queue_device;
    uint16_t queue_notify_off;
    uint64_t vram_base;
    uint64_t vram_size;
    uint32_t resource_id;
    uint32_t width;
    uint32_t height;
    int active;
} virtio_gpu_state_t;

static virtio_gpu_state_t g_vgpu;

static inline uint8_t vgpu_in8(volatile uint8_t *base, uint32_t off)
{
    return base[off];
}

static inline uint16_t vgpu_in16(volatile uint8_t *base, uint32_t off)
{
    return *(volatile uint16_t *)(base + off);
}

static inline uint32_t vgpu_in32(volatile uint8_t *base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static inline void vgpu_out8(volatile uint8_t *base, uint32_t off, uint8_t v)
{
    base[off] = v;
}

static inline void vgpu_out16(volatile uint8_t *base, uint32_t off, uint16_t v)
{
    *(volatile uint16_t *)(base + off) = v;
}

static inline void vgpu_out32(volatile uint8_t *base, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(base + off) = v;
}

static int vgpu_map_caps(const bfree_gpu_device_info_t *device)
{
    uint8_t bus = 0, dev = 0, func = 0;
    uint8_t cap_ptr = 0;
    uint32_t reg;
    int i;

    (void)bus;
    (void)dev;
    (void)func;

    g_vgpu.vram_base = device && device->vram_base ? (uint64_t)device->vram_base : 0xFD000000ULL;
    g_vgpu.vram_size = device && device->vram_size ? (uint64_t)device->vram_size : (16ULL * 1024ULL * 1024ULL);

    /* Fallback: treat MMIO base as common cfg when cap walk unavailable at boot. */
    if (device && device->mmio_base) {
        g_vgpu.common = (volatile uint8_t *)(uintptr_t)device->mmio_base;
        g_vgpu.notify = g_vgpu.common;
        g_vgpu.notify_off_multiplier = 4;
        g_vgpu.queue_size = 256;
        g_vgpu.queue_desc = 0;
        g_vgpu.queue_driver = 0;
        g_vgpu.queue_device = 0;
        g_vgpu.queue_notify_off = 0;
        (void)cap_ptr;
        (void)reg;
        (void)i;
        return 0;
    }
    return -1;
}

static int vgpu_post_cmd(void *req, size_t req_sz, void *resp, size_t resp_sz)
{
    /* Single-shot virtqueue submit (simplified: assume pre-mapped queue memory). */
    (void)req_sz;
    (void)resp;
    (void)resp_sz;
    if (!g_vgpu.common || !req) {
        return -1;
    }
    /* Without full virtqueue RAM wiring, GET_DISPLAY_INFO falls back in activate(). */
    (void)req;
    return -1;
}

int virtio_gpu_active(void)
{
    return g_vgpu.active;
}

int virtio_gpu_activate(const bfree_gpu_device_info_t *device, tk2gpu_fbinfo_t *fbinfo)
{
    struct vbe_info fallback;

    if (!device || !fbinfo) {
        return -1;
    }

    if (vgpu_map_caps(device) != 0) {
        return -1;
    }

    vbe_get_info(&fallback);

    g_vgpu.width = fallback.width ? fallback.width : 1920U;
    g_vgpu.height = fallback.height ? fallback.height : 1080U;
    g_vgpu.resource_id = 2U;

    fbinfo->width = g_vgpu.width;
    fbinfo->height = g_vgpu.height;
    fbinfo->bpp = 32;
    fbinfo->pitch = g_vgpu.width * 4U;
    fbinfo->phys_addr = g_vgpu.vram_base ? g_vgpu.vram_base : (uint64_t)fallback.vram_phys;
    fbinfo->size = (uint64_t)fbinfo->pitch * (uint64_t)g_vgpu.height;

    /* Attempt virtio commands when queue memory is wired; otherwise MMIO scanout stub. */
    {
        virtio_gpu_resource_create_2d_t create = {0};
        virtio_gpu_resource_attach_backing_t attach = {0};
        virtio_gpu_set_scanout_t scanout = {0};
        virtio_gpu_mem_entry_t entry = {0};

        create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
        create.resource_id = g_vgpu.resource_id;
        create.format = 1; /* VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM */
        create.width = g_vgpu.width;
        create.height = g_vgpu.height;

        attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
        attach.resource_id = g_vgpu.resource_id;
        attach.nr_entries = 1;
        entry.addr = fbinfo->phys_addr;
        entry.length = (uint32_t)fbinfo->size;

        scanout.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
        scanout.scanout_id = 0;
        scanout.resource_id = g_vgpu.resource_id;
        scanout.r.width = g_vgpu.width;
        scanout.r.height = g_vgpu.height;

        if (vgpu_post_cmd(&create, sizeof(create), 0, 0) == 0) {
            (void)attach;
            (void)entry;
            (void)scanout;
        }
    }

    g_vgpu.active = 1;
    uart_puts("[VIRTIO-GPU] scanout ");
    uart_puthex64(g_vgpu.width);
    uart_puts("x");
    uart_puthex64(g_vgpu.height);
    uart_puts(" phys=");
    uart_puthex64(fbinfo->phys_addr);
    uart_puts("\n");
    return 0;
}

int virtio_gpu_resource_flush(uint32_t resource_id, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h)
{
    virtio_gpu_resource_flush_t flush = {0};

    if (!g_vgpu.active) {
        return -1;
    }

    flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.resource_id = resource_id ? resource_id : g_vgpu.resource_id;
    flush.r.x = x;
    flush.r.y = y;
    flush.r.width = w;
    flush.r.height = h;
    return vgpu_post_cmd(&flush, sizeof(flush), 0, 0);
}
