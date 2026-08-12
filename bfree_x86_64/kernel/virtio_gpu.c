/*
 * virtio-gpu (PCI 1af4:1050) backend — modern PCI virtqueue + scanout setup.
 */
#include <stdint.h>
#include <stddef.h>
#include "virtio_gpu.h"
#include "gpu_backend.h"
#include "vbe_gop.h"

extern void uart_puts(const char *);
extern void uart_puthex64(uint64_t);

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_CAP_VENDOR 0x09
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2

#define VIRTIO_F_VERSION_1 32

#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED    128

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO        0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      0x0101
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_SET_SCANOUT             0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH          0x0104

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1
#define VIRTIO_GPU_MAX_SCANOUTS 16
#define VGPU_QUEUE_SIZE 32
#define VGPU_QUEUE_PAGES 4

static uint8_t g_vq_storage[VGPU_QUEUE_PAGES * 4096] __attribute__((aligned(4096)));

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed)) vgpu_ctrl_hdr_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) vgpu_rect_t;

typedef struct {
    vgpu_rect_t r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed)) vgpu_display_mode_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    vgpu_display_mode_t pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed)) vgpu_resp_display_info_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) vgpu_resource_create_2d_t;

typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) vgpu_mem_entry_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    vgpu_mem_entry_t entry;
} __attribute__((packed)) vgpu_attach_backing_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    uint32_t scanout_id;
    uint32_t resource_id;
    vgpu_rect_t r;
} __attribute__((packed)) vgpu_set_scanout_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    vgpu_rect_t r;
    uint32_t padding;
} __attribute__((packed)) vgpu_resource_flush_t;

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) vring_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VGPU_QUEUE_SIZE];
    uint16_t unused;
} __attribute__((packed)) vring_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) vring_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    vring_used_elem_t ring[VGPU_QUEUE_SIZE];
    uint16_t unused;
} __attribute__((packed)) vring_used_t;

typedef struct {
    volatile uint8_t *common_cfg;
    volatile uint8_t *notify_cfg;
    uint32_t notify_off_multiplier;
    uint32_t queue_size;
    uint16_t queue_notify_off;
    uint64_t queue_desc_phys;
    uint64_t queue_avail_phys;
    uint64_t queue_used_phys;
    vring_desc_t *desc;
    vring_avail_t *avail;
    vring_used_t *used;
    uint16_t last_used_idx;
    uint8_t pci_bus;
    uint8_t pci_dev;
    uint8_t pci_func;
    uint64_t vram_base;
    uint64_t vram_size;
    uint32_t resource_id;
    uint32_t width;
    uint32_t height;
    int queue_ready;
    int active;
} virtio_gpu_state_t;

static virtio_gpu_state_t g_vgpu;

static void pci_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static uint32_t pci_inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFCU);
    pci_outl(PCI_CONFIG_ADDRESS, addr);
    return pci_inl(PCI_CONFIG_DATA);
}

static void pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val)
{
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFCU);
    pci_outl(PCI_CONFIG_ADDRESS, addr);
    pci_outl(PCI_CONFIG_DATA, val);
}

static uint8_t pci_find_cap(uint8_t bus, uint8_t dev, uint8_t func, uint8_t cap_id)
{
    uint32_t h0 = pci_cfg_read32(bus, dev, func, 0x00);
    if ((h0 & 0xFFFFU) == 0xFFFFU) {
        return 0;
    }
    if ((pci_cfg_read32(bus, dev, func, 0x04) & 0x00100000U) == 0U) {
        return 0;
    }
    uint8_t ptr = (uint8_t)(pci_cfg_read32(bus, dev, func, 0x34) & 0xFFU);
    while (ptr >= 0x40U) {
        uint32_t cap = pci_cfg_read32(bus, dev, func, ptr);
        if ((cap & 0xFFU) == cap_id) {
            return ptr;
        }
        ptr = (uint8_t)((cap >> 8) & 0xFFU);
    }
    return 0;
}

static volatile uint8_t *pci_map_bar(uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_off)
{
    uint32_t low = pci_cfg_read32(bus, dev, func, bar_off);
    if (low == 0U || (low & 1U) != 0U) {
        return 0;
    }
    return (volatile uint8_t *)(uintptr_t)(low & ~0xFU);
}

static volatile uint8_t *virtio_map_cap(uint8_t bus, uint8_t dev, uint8_t func, uint8_t cfg_type)
{
    uint8_t ptr = pci_find_cap(bus, dev, func, PCI_CAP_VENDOR);

    while (ptr >= 0x40U) {
        uint32_t hdr = pci_cfg_read32(bus, dev, func, ptr);
        uint8_t cap_type = (uint8_t)((hdr >> 24) & 0xFFU);

        if (cap_type == cfg_type) {
            uint8_t bar = (uint8_t)(pci_cfg_read32(bus, dev, func, ptr + 4) & 0xFFU);
            uint32_t offset = pci_cfg_read32(bus, dev, func, ptr + 8);
            volatile uint8_t *bar_base = pci_map_bar(bus, dev, func, (uint8_t)(0x10 + bar * 4));

            if (bar_base) {
                return bar_base + offset;
            }
        }
        ptr = (uint8_t)((hdr >> 8) & 0xFFU);
    }
    return 0;
}

static uint8_t vgpu_common_read8(uint32_t off)
{
    return g_vgpu.common_cfg ? g_vgpu.common_cfg[off] : 0;
}

static uint16_t vgpu_common_read16(uint32_t off)
{
    return *(volatile uint16_t *)(g_vgpu.common_cfg + off);
}

static uint32_t vgpu_common_read32(uint32_t off)
{
    return *(volatile uint32_t *)(g_vgpu.common_cfg + off);
}

static void vgpu_common_write8(uint32_t off, uint8_t v)
{
    if (g_vgpu.common_cfg) {
        g_vgpu.common_cfg[off] = v;
    }
}

static void vgpu_common_write16(uint32_t off, uint16_t v)
{
    *(volatile uint16_t *)(g_vgpu.common_cfg + off) = v;
}

static void vgpu_common_write32(uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(g_vgpu.common_cfg + off) = v;
}

static void vgpu_common_write64(uint32_t off, uint64_t v)
{
    vgpu_common_write32(off, (uint32_t)v);
    vgpu_common_write32(off + 4, (uint32_t)(v >> 32));
}

static int vgpu_queue_init(void)
{
    uint32_t desc_bytes;
    uint32_t avail_off;
    uint32_t used_off;
    uint8_t *mem = g_vq_storage;

    if (!g_vgpu.common_cfg) {
        return -1;
    }

    desc_bytes = VGPU_QUEUE_SIZE * (uint32_t)sizeof(vring_desc_t);
    avail_off = (desc_bytes + 4095U) & ~4095U;
    used_off = (avail_off + (uint32_t)offsetof(vring_avail_t, ring) +
                VGPU_QUEUE_SIZE * 2U + 2U + 4095U) & ~4095U;

    g_vgpu.queue_desc_phys = (uint64_t)(uintptr_t)mem;
    g_vgpu.queue_avail_phys = g_vgpu.queue_desc_phys + avail_off;
    g_vgpu.queue_used_phys = g_vgpu.queue_desc_phys + used_off;
    g_vgpu.desc = (vring_desc_t *)mem;
    g_vgpu.avail = (vring_avail_t *)(mem + avail_off);
    g_vgpu.used = (vring_used_t *)(mem + used_off);
    g_vgpu.last_used_idx = 0;
    g_vgpu.queue_size = VGPU_QUEUE_SIZE;
    g_vgpu.avail->flags = 0;
    g_vgpu.avail->idx = 0;
    g_vgpu.used->flags = 0;
    g_vgpu.used->idx = 0;

    vgpu_common_write16(0x16, 0); /* queue_select */
    {
        uint16_t qsz = vgpu_common_read16(0x18);
        if (qsz < VGPU_QUEUE_SIZE) {
            g_vgpu.queue_size = qsz;
        }
    }
    vgpu_common_write64(0x20, g_vgpu.queue_desc_phys);
    vgpu_common_write64(0x28, g_vgpu.queue_avail_phys);
    vgpu_common_write64(0x30, g_vgpu.queue_used_phys);
    g_vgpu.queue_notify_off = vgpu_common_read16(0x1E);
    vgpu_common_write16(0x1C, 1); /* queue_enable */
    g_vgpu.queue_ready = 1;
    uart_puts("[VIRTIO-GPU] queue ready size=");
    uart_puthex64(g_vgpu.queue_size);
    uart_puts("\n");
    return 0;
}

static int vgpu_device_init(const bfree_gpu_device_info_t *device)
{
    uint32_t cmd;
    uint32_t tries;

    g_vgpu.pci_bus = device->pci_bus;
    g_vgpu.pci_dev = device->pci_dev;
    g_vgpu.pci_func = device->pci_func;
    g_vgpu.vram_base = device->vram_base ? (uint64_t)device->vram_base : 0xFD000000ULL;
    g_vgpu.vram_size = device->vram_size ? (uint64_t)device->vram_size : (16ULL << 20);

    cmd = pci_cfg_read32(device->pci_bus, device->pci_dev, device->pci_func, 0x04);
    pci_cfg_write32(device->pci_bus, device->pci_dev, device->pci_func, 0x04,
                    cmd | 0x0006U); /* MEM + bus master */

    g_vgpu.common_cfg = virtio_map_cap(device->pci_bus, device->pci_dev, device->pci_func,
                                       VIRTIO_PCI_CAP_COMMON_CFG);
    g_vgpu.notify_cfg = virtio_map_cap(device->pci_bus, device->pci_dev, device->pci_func,
                                       VIRTIO_PCI_CAP_NOTIFY_CFG);
    if (!g_vgpu.common_cfg) {
        uart_puts("[VIRTIO-GPU] no common cfg cap\n");
        return -1;
    }

    g_vgpu.notify_off_multiplier = 4;
    {
        uint8_t ptr = pci_find_cap(device->pci_bus, device->pci_dev, device->pci_func, PCI_CAP_VENDOR);
        while (ptr >= 0x40U) {
            uint32_t hdr = pci_cfg_read32(device->pci_bus, device->pci_dev, device->pci_func, ptr);
            if (((hdr >> 24) & 0xFFU) == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                g_vgpu.notify_off_multiplier =
                    pci_cfg_read32(device->pci_bus, device->pci_dev, device->pci_func, ptr + 16);
                if (g_vgpu.notify_off_multiplier == 0U) {
                    g_vgpu.notify_off_multiplier = 4;
                }
                break;
            }
            ptr = (uint8_t)((hdr >> 8) & 0xFFU);
        }
    }

    vgpu_common_write8(0x14, 0); /* reset */
    for (tries = 0; tries < 100000U; ++tries) {
        __asm__ volatile ("pause");
    }

    vgpu_common_write8(0x14, VIRTIO_STATUS_ACK);
    vgpu_common_write8(0x14, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    vgpu_common_write32(0x00, 1);
    vgpu_common_write32(0x04, 0);
    vgpu_common_write32(0x08, 1);
    vgpu_common_write32(0x0C, 1U << VIRTIO_F_VERSION_1);
    vgpu_common_write8(0x14, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    if (vgpu_queue_init() != 0) {
        vgpu_common_write8(0x14, VIRTIO_STATUS_FAILED);
        return -1;
    }

    vgpu_common_write8(0x14, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                               VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    return 0;
}

static void vgpu_notify(void)
{
    if (!g_vgpu.notify_cfg) {
        return;
    }
    *(volatile uint16_t *)(g_vgpu.notify_cfg + (uint64_t)g_vgpu.queue_notify_off *
                           (uint64_t)g_vgpu.notify_off_multiplier) = 0;
}

static int vgpu_post_cmd(const void *req, uint32_t req_len, void *resp, uint32_t resp_len)
{
    uint16_t avail_idx;
    uint16_t used_idx;
    uint32_t spin;

    if (!g_vgpu.queue_ready || !req || !resp || req_len == 0 || resp_len == 0) {
        return -1;
    }

    g_vgpu.desc[0].addr = (uint64_t)(uintptr_t)req;
    g_vgpu.desc[0].len = req_len;
    g_vgpu.desc[0].flags = VRING_DESC_F_NEXT;
    g_vgpu.desc[0].next = 1;

    g_vgpu.desc[1].addr = (uint64_t)(uintptr_t)resp;
    g_vgpu.desc[1].len = resp_len;
    g_vgpu.desc[1].flags = VRING_DESC_F_WRITE;
    g_vgpu.desc[1].next = 0;

    avail_idx = g_vgpu.avail->idx;
    g_vgpu.avail->ring[avail_idx % g_vgpu.queue_size] = 0;
    __asm__ volatile ("" ::: "memory");
    g_vgpu.avail->idx = avail_idx + 1;
    vgpu_notify();

    for (spin = 0; spin < 5000000U; ++spin) {
        used_idx = g_vgpu.used->idx;
        if (used_idx != g_vgpu.last_used_idx) {
            g_vgpu.last_used_idx = used_idx;
            return 0;
        }
        __asm__ volatile ("pause");
    }
    return -1;
}

static int vgpu_get_display_info(uint32_t *w, uint32_t *h)
{
    vgpu_ctrl_hdr_t req = {0};
    vgpu_resp_display_info_t resp;
    uint32_t i;

    req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    if (vgpu_post_cmd(&req, sizeof(req), &resp, sizeof(resp)) != 0) {
        return -1;
    }
    for (i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; ++i) {
        if (resp.pmodes[i].enabled && resp.pmodes[i].r.width && resp.pmodes[i].r.height) {
            *w = resp.pmodes[i].r.width;
            *h = resp.pmodes[i].r.height;
            return 0;
        }
    }
    return -1;
}

static int vgpu_setup_scanout(uint64_t backing_phys, uint32_t w, uint32_t h, uint32_t size_bytes)
{
    vgpu_resource_create_2d_t create = {0};
    vgpu_attach_backing_t attach = {0};
    vgpu_set_scanout_t scanout = {0};

    create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create.resource_id = g_vgpu.resource_id;
    create.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    create.width = w;
    create.height = h;
    if (vgpu_post_cmd(&create, sizeof(create), &create, sizeof(create)) != 0) {
        return -1;
    }

    attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach.resource_id = g_vgpu.resource_id;
    attach.nr_entries = 1;
    attach.entry.addr = backing_phys;
    attach.entry.length = size_bytes;
    if (vgpu_post_cmd(&attach, sizeof(attach), &attach, sizeof(attach)) != 0) {
        return -1;
    }

    scanout.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout.scanout_id = 0;
    scanout.resource_id = g_vgpu.resource_id;
    scanout.r.width = w;
    scanout.r.height = h;
    if (vgpu_post_cmd(&scanout, sizeof(scanout), &scanout, sizeof(scanout)) != 0) {
        return -1;
    }
    return 0;
}

int virtio_gpu_active(void)
{
    return g_vgpu.active;
}

int virtio_gpu_activate(const bfree_gpu_device_info_t *device, tk2gpu_fbinfo_t *fbinfo)
{
    struct vbe_info fallback;
    uint32_t w = 1920U;
    uint32_t h = 1080U;
    uint64_t phys;
    uint64_t size_bytes;

    if (!device || !fbinfo) {
        return -1;
    }

    vbe_get_info(&fallback);
    g_vgpu.resource_id = 2U;
    g_vgpu.active = 0;
    g_vgpu.queue_ready = 0;

    if (vgpu_device_init(device) != 0) {
        return -1;
    }

    if (vgpu_get_display_info(&w, &h) != 0) {
        w = fallback.width ? fallback.width : 1920U;
        h = fallback.height ? fallback.height : 1080U;
        uart_puts("[VIRTIO-GPU] GET_DISPLAY_INFO fallback\n");
    }

    g_vgpu.width = w;
    g_vgpu.height = h;
    phys = g_vgpu.vram_base ? g_vgpu.vram_base : (uint64_t)fallback.vram_phys;
    size_bytes = (uint64_t)w * 4ULL * (uint64_t)h;

    if (vgpu_setup_scanout(phys, w, h, (uint32_t)size_bytes) != 0) {
        uart_puts("[VIRTIO-GPU] scanout setup failed (direct VRAM fallback)\n");
    } else {
        uart_puts("[VIRTIO-GPU] scanout ok\n");
    }

    fbinfo->width = w;
    fbinfo->height = h;
    fbinfo->bpp = 32;
    fbinfo->pitch = w * 4U;
    fbinfo->phys_addr = phys;
    fbinfo->size = size_bytes;
    g_vgpu.active = 1;

    uart_puts("[VIRTIO-GPU] ");
    uart_puthex64(w);
    uart_puts("x");
    uart_puthex64(h);
    uart_puts(" phys=");
    uart_puthex64(phys);
    uart_puts("\n");
    return 0;
}

int virtio_gpu_resource_flush(uint32_t resource_id, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h)
{
    vgpu_resource_flush_t flush = {0};
    vgpu_resource_flush_t resp;

    if (!g_vgpu.active) {
        return -1;
    }

    flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.resource_id = resource_id ? resource_id : g_vgpu.resource_id;
    flush.r.x = x;
    flush.r.y = y;
    flush.r.width = w;
    flush.r.height = h;
    resp = flush;
    return vgpu_post_cmd(&flush, sizeof(flush), &resp, sizeof(resp));
}
