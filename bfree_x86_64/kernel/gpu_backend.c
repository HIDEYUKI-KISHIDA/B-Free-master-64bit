#include "gpu_backend.h"
#include "cache.h"
#include "vbe_gop.h"
#include "virtio_gpu.h"

#define PCI_VENDOR_QEMU_LEGACY 0x1234
#define PCI_DEVICE_QEMU_STD_VGA 0x1111
#define PCI_VENDOR_QXL 0x1b36
#define PCI_DEVICE_QXL_VGA 0x0100
#define PCI_VENDOR_VIRTIO 0x1af4
#define PCI_DEVICE_VIRTIO_VGA 0x1050
#define PCI_VENDOR_VMWARE 0x15ad
#define PCI_DEVICE_VMWARE_SVGA 0x0405

#define VBE_DISPI_IOPORT_INDEX      0x01CE
#define VBE_DISPI_IOPORT_DATA       0x01CF
#define VBE_DISPI_INDEX_ID          0x0
#define VBE_DISPI_INDEX_XRES        0x1
#define VBE_DISPI_INDEX_YRES        0x2
#define VBE_DISPI_INDEX_BPP         0x3
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9

/* 標準VGA 入力ステータスレジスタ1 (VBlank検出用) */
#define VGA_INPUT_STATUS_1          0x3DA
#define VGA_VBLANK_BIT              0x08

#define BFREE_GPU_MAX_BUFFERS 4

typedef struct bfree_gpu_buffer_slot {
    uint32_t handle;
    uint64_t phys_addr;
    uint64_t offset;
    uint64_t size;
    int in_use;
} bfree_gpu_buffer_slot_t;

static bfree_gpu_backend_state_t g_gpu_backend_state;
static bfree_gpu_buffer_slot_t g_gpu_buffers[BFREE_GPU_MAX_BUFFERS];
static uint32_t g_next_gpu_buffer_handle = 1;

static void gpu_copy_cstr(char *dst, size_t dst_size, const char *src)
{
    size_t index = 0;
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (index + 1 < dst_size && src[index] != '\0') {
        dst[index] = src[index];
        ++index;
    }
    dst[index] = '\0';
}

static void outw16(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static uint16_t inw16(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t inb8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t vbe_dispi_read(uint16_t index)
{
    outw16(VBE_DISPI_IOPORT_INDEX, index);
    return inw16(VBE_DISPI_IOPORT_DATA);
}

static int is_bochs_qemu_vga_device(const bfree_gpu_device_info_t *device)
{
    return device
        && device->vendor_id == PCI_VENDOR_QEMU_LEGACY
        && device->device_id == PCI_DEVICE_QEMU_STD_VGA;
}

static void gpu_reset_buffers(void)
{
    int index;

    for (index = 0; index < BFREE_GPU_MAX_BUFFERS; ++index) {
        g_gpu_buffers[index].handle = 0;
        g_gpu_buffers[index].phys_addr = 0;
        g_gpu_buffers[index].offset = 0;
        g_gpu_buffers[index].size = 0;
        g_gpu_buffers[index].in_use = 0;
    }

    g_next_gpu_buffer_handle = 1;
}

static bfree_gpu_buffer_slot_t *gpu_find_buffer_slot(uint32_t handle)
{
    int index;

    for (index = 0; index < BFREE_GPU_MAX_BUFFERS; ++index) {
        if (g_gpu_buffers[index].in_use && g_gpu_buffers[index].handle == handle) {
            return &g_gpu_buffers[index];
        }
    }

    return 0;
}

static bfree_gpu_buffer_slot_t *gpu_alloc_buffer_slot(void)
{
    int index;

    for (index = 0; index < BFREE_GPU_MAX_BUFFERS; ++index) {
        if (!g_gpu_buffers[index].in_use) {
            return &g_gpu_buffers[index];
        }
    }

    return 0;
}

static uint64_t gpu_frame_buffer_size_bytes(const tk2gpu_fbinfo_t *fbinfo)
{
    uint64_t pitch;
    uint64_t height;

    if (!fbinfo) {
        return 0;
    }

    pitch = fbinfo->pitch;
    height = fbinfo->height;
    if (pitch == 0 || height == 0) {
        return 0;
    }

    return pitch * height;
}

static const char *gpu_device_family_name(const bfree_gpu_device_info_t *device)
{
    if (!device) {
        return "none";
    }

    if (device->vendor_id == PCI_VENDOR_VIRTIO && device->device_id == PCI_DEVICE_VIRTIO_VGA) {
        return "virtio-gpu";
    }
    if (device->vendor_id == PCI_VENDOR_QEMU_LEGACY && device->device_id == PCI_DEVICE_QEMU_STD_VGA) {
        return "bochs-qemu-vga";
    }
    if (device->vendor_id == PCI_VENDOR_QXL && device->device_id == PCI_DEVICE_QXL_VGA) {
        return "qxl";
    }
    if (device->vendor_id == PCI_VENDOR_VMWARE && device->device_id == PCI_DEVICE_VMWARE_SVGA) {
        return "vmware-svga";
    }

    return "unknown-display";
}

static int framebuffer_backend_init(const bfree_gpu_device_info_t *device, tk2gpu_fbinfo_t *fbinfo)
{
    struct vbe_info info;

    (void)device;
    vbe_get_info(&info);
    fbinfo->width = info.width;
    fbinfo->height = info.height;
    fbinfo->bpp = info.bpp;
    fbinfo->pitch = info.pitch;
    fbinfo->phys_addr = (uint64_t)info.vram_phys;
    fbinfo->size = (uint64_t)info.vram_size;
    return 0;
}

static int framebuffer_backend_wait_vblank(void)
{
    /* framebuffer backend: WAIT_VBLANK is a supported no-op. */
    return TK2GPU_OK;
}

static int framebuffer_backend_page_flip(uint32_t buffer_handle)
{
    /* framebuffer backend only guarantees front buffer (handle=0). */
    if (buffer_handle == 0U) {
        g_gpu_backend_state.current_buffer_handle = 0U;
        return TK2GPU_OK;
    }
    return TK2GPU_ENOSYS;
}

static void framebuffer_backend_shutdown(void)
{
}

static int mmio_gpu_backend_init(const bfree_gpu_device_info_t *device, tk2gpu_fbinfo_t *fbinfo)
{
    struct vbe_info fallback_info;

    if (!device || device->mmio_base == 0) {
        return -1;
    }

    vbe_get_info(&fallback_info);

    if (device->vendor_id == PCI_VENDOR_VIRTIO && device->device_id == PCI_DEVICE_VIRTIO_VGA) {
        if (virtio_gpu_activate(device, fbinfo) == 0) {
            return 0;
        }
        fbinfo->width = fallback_info.width;
        fbinfo->height = fallback_info.height;
        fbinfo->bpp = fallback_info.bpp;
        fbinfo->pitch = fallback_info.pitch;
        fbinfo->phys_addr = (uint64_t)fallback_info.vram_phys;
        fbinfo->size = 16U * 1024U * 1024U;
        return 0;
    }

    if (device->vendor_id == PCI_VENDOR_QEMU_LEGACY && device->device_id == PCI_DEVICE_QEMU_STD_VGA) {
        uint16_t xres = vbe_dispi_read(VBE_DISPI_INDEX_XRES);
        uint16_t yres = vbe_dispi_read(VBE_DISPI_INDEX_YRES);
        uint16_t bpp = vbe_dispi_read(VBE_DISPI_INDEX_BPP);
        uint16_t dispi_id = vbe_dispi_read(VBE_DISPI_INDEX_ID);

        if (dispi_id == 0 || xres == 0 || yres == 0 || bpp == 0) {
            return -1;
        }

        fbinfo->width  = xres;
        fbinfo->height = yres;
        fbinfo->bpp    = bpp;
        fbinfo->pitch  = xres * ((bpp + 7U) / 8U);
        fbinfo->phys_addr = device->vram_base != 0 ? (uint64_t)device->vram_base : (uint64_t)fallback_info.vram_phys;

        /* G3: VIRT_HEIGHT を 2*yres に設定してダブルバッファ領域を確保 */
        outw16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_VIRT_HEIGHT);
        outw16(VBE_DISPI_IOPORT_DATA,  (uint16_t)(yres * 2U));
        /* Y_OFFSET を 0 にリセット (フロントバッファを表示) */
        outw16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_Y_OFFSET);
        outw16(VBE_DISPI_IOPORT_DATA,  0);

        /* sizeはダブルバッファ分を確保 (pitch * yres * 2) */
        fbinfo->size = (uint64_t)fbinfo->pitch * (uint64_t)(yres * 2U);
        return 0;
    }

    if (device->vendor_id == PCI_VENDOR_QXL && device->device_id == PCI_DEVICE_QXL_VGA) {
        fbinfo->width = fallback_info.width;
        fbinfo->height = fallback_info.height;
        fbinfo->bpp = fallback_info.bpp;
        fbinfo->pitch = fallback_info.pitch;
        fbinfo->phys_addr = (uint64_t)fallback_info.vram_phys;
        fbinfo->size = 16U * 1024U * 1024U;
        return 0;
    }

    if (device->vendor_id == PCI_VENDOR_VMWARE && device->device_id == PCI_DEVICE_VMWARE_SVGA) {
        fbinfo->width = fallback_info.width;
        fbinfo->height = fallback_info.height;
        fbinfo->bpp = fallback_info.bpp;
        fbinfo->pitch = fallback_info.pitch;
        fbinfo->phys_addr = (uint64_t)fallback_info.vram_phys;
        fbinfo->size = 16U * 1024U * 1024U;
        return 0;
    }

    return -1;
}

/**
 * キャッシュコヒーレンシの確保
 * バッファ書き込み後に実行し、GPUからの読み取り整合性を保証する
 */
void gpu_cache_flush_buffer(void *vaddr, size_t size) {
    if (vaddr && size > 0) {
        cache_flush_range(vaddr, size);
    }
}

static int mmio_gpu_backend_wait_vblank(void)
{
    if (is_bochs_qemu_vga_device(&g_gpu_backend_state.device)) {
        uint32_t timeout;
        /* VBlank中なら抜けるまで待つ (最大50万ポーリング) */
        timeout = 500000U;
        while ((inb8(VGA_INPUT_STATUS_1) & VGA_VBLANK_BIT) && timeout > 0U) {
            --timeout;
        }
        /* 次のVBlank開始を待つ (最大200万ポーリング @60Hz ≒ 16.7ms) */
        timeout = 2000000U;
        while (!(inb8(VGA_INPUT_STATUS_1) & VGA_VBLANK_BIT) && timeout > 0U) {
            --timeout;
        }
        return TK2GPU_OK;
    }
    /* Unknown MMIO GPU family: keep API callable as no-op. */
    return TK2GPU_OK;
}

static int mmio_gpu_backend_page_flip(uint32_t buffer_handle)
{
    if (is_bochs_qemu_vga_device(&g_gpu_backend_state.device)) {
        bfree_gpu_buffer_slot_t *slot;
        uint32_t pitch;
        uint16_t y_offset;

        pitch = g_gpu_backend_state.fbinfo.pitch;
        if (pitch == 0U) {
            return TK2GPU_EIO;
        }

        if (buffer_handle == 0U) {
            /* フロントバッファ(Y_OFFSET=0)に戻す */
            outw16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_Y_OFFSET);
            outw16(VBE_DISPI_IOPORT_DATA,  0);
            g_gpu_backend_state.current_buffer_handle = 0U;
            return TK2GPU_OK;
        }

        slot = gpu_find_buffer_slot(buffer_handle);
        if (slot != 0) {
            /* 最適化: フリップ前に当該バッファのキャッシュをフラッシュ */
            /* 注意: vaddrの解決にはメモ理マッピング情報の参照が必要 */
            // gpu_cache_flush_buffer(slot->vaddr, slot->size);

            /* バックバッファのY開始行 = offset / pitch */
            y_offset = (uint16_t)(slot->offset / (uint64_t)pitch);
            /* G3: BGA Y_OFFSET レジスタへハードウェア書き込み */
            outw16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_Y_OFFSET);
            outw16(VBE_DISPI_IOPORT_DATA,  y_offset);
            g_gpu_backend_state.fbinfo.phys_addr    = slot->phys_addr;
            g_gpu_backend_state.current_buffer_handle = buffer_handle;
            return TK2GPU_OK;
        }

        return TK2GPU_ENOENT;
    }

    (void)buffer_handle;
    return TK2GPU_ENOSYS;
}

static void mmio_gpu_backend_shutdown(void)
{
}

static const bfree_gpu_backend_ops_t g_framebuffer_backend_ops = {
    framebuffer_backend_init,
    framebuffer_backend_wait_vblank,
    framebuffer_backend_page_flip,
    framebuffer_backend_shutdown,
};

static const bfree_gpu_backend_ops_t g_mmio_gpu_backend_ops = {
    mmio_gpu_backend_init,
    mmio_gpu_backend_wait_vblank,
    mmio_gpu_backend_page_flip,
    mmio_gpu_backend_shutdown,
};

void gpu_backend_init(void)
{
    gpu_reset_buffers();
    g_gpu_backend_state.active_backend = BFREE_GPU_BACKEND_FRAMEBUFFER;
    g_gpu_backend_state.gpu_present = 0;
    g_gpu_backend_state.initialized = 1;
    g_gpu_backend_state.current_buffer_handle = 0;
    g_gpu_backend_state.device_name = "framebuffer";
    g_gpu_backend_state.ops = &g_framebuffer_backend_ops;
    g_gpu_backend_state.ops->init(0, &g_gpu_backend_state.fbinfo);
}

int gpu_backend_try_activate_gpu(const bfree_gpu_device_info_t *device)
{
    if (!g_gpu_backend_state.initialized) {
        gpu_backend_init();
    }

    if (!device) {
        return -1;
    }

    g_gpu_backend_state.device = *device;
    g_gpu_backend_state.gpu_present = 1;
    g_gpu_backend_state.current_buffer_handle = 0;
    g_gpu_backend_state.device_name = gpu_device_family_name(device);
    gpu_reset_buffers();

    if (g_mmio_gpu_backend_ops.init(device, &g_gpu_backend_state.fbinfo) != 0) {
        g_gpu_backend_state.active_backend = BFREE_GPU_BACKEND_FRAMEBUFFER;
        g_gpu_backend_state.ops = &g_framebuffer_backend_ops;
        g_gpu_backend_state.device_name = "framebuffer";
        g_gpu_backend_state.ops->init(0, &g_gpu_backend_state.fbinfo);
        return -1;
    }

    g_gpu_backend_state.active_backend = BFREE_GPU_BACKEND_MMIO_GPU;
    g_gpu_backend_state.ops = &g_mmio_gpu_backend_ops;
    return 0;
}

int gpu_backend_active_type(void)
{
    if (!g_gpu_backend_state.initialized) {
        gpu_backend_init();
    }
    return g_gpu_backend_state.active_backend;
}

const char *gpu_backend_active_name(void)
{
    if (gpu_backend_active_type() == BFREE_GPU_BACKEND_MMIO_GPU) {
        return "mmio-gpu";
    }
    return "framebuffer";
}

const char *gpu_backend_device_name(void)
{
    if (!g_gpu_backend_state.initialized) {
        gpu_backend_init();
    }
    return g_gpu_backend_state.device_name ? g_gpu_backend_state.device_name : "framebuffer";
}

const bfree_gpu_backend_state_t *gpu_backend_state(void)
{
    if (!g_gpu_backend_state.initialized) {
        gpu_backend_init();
    }
    return &g_gpu_backend_state;
}

int gpu_backend_ioctl(int cmd, void *arg)
{
    const bfree_gpu_backend_state_t *state = gpu_backend_state();

    if (!state || !state->ops) {
        return TK2GPU_EIO;
    }

    switch (cmd) {
    case TK2GPU_IOCTL_GET_INFO:
        if (!arg) {
            return TK2GPU_EINVAL;
        }
        *(tk2gpu_fbinfo_t *)arg = state->fbinfo;
        return TK2GPU_OK;
    case TK2GPU_IOCTL_WAIT_VBLANK:
        return state->ops->wait_vblank ? state->ops->wait_vblank() : TK2GPU_ENOSYS;
    case TK2GPU_IOCTL_PAGE_FLIP:
        if (!arg) {
            return TK2GPU_EINVAL;
        }
        return state->ops->page_flip ? state->ops->page_flip(((tk2gpu_buffer_t*)arg)->handle) : TK2GPU_ENOSYS;

    case TK2GPU_IOCTL_ALLOC_BUFFER:
        if (is_bochs_qemu_vga_device(&state->device)) {
            uint64_t buf_size;
            tk2gpu_buffer_t *out;
            bfree_gpu_buffer_slot_t *slot;
            if (!arg) return TK2GPU_EINVAL;
            slot = gpu_alloc_buffer_slot();
            if (slot == 0) return TK2GPU_ENOSYS;
            
            buf_size = gpu_frame_buffer_size_bytes(&state->fbinfo);
            slot->offset = buf_size * (uint64_t)(slot - g_gpu_buffers);
            if (slot->offset + buf_size > state->fbinfo.size) return TK2GPU_ENOSYS;

            slot->handle = g_next_gpu_buffer_handle++;
            slot->phys_addr = state->fbinfo.phys_addr + slot->offset;
            slot->size = buf_size;
            slot->in_use = 1;

            out = (tk2gpu_buffer_t *)arg;
            out->handle = slot->handle;
            out->phys_addr = slot->phys_addr;
            out->vaddr = 0;
            out->offset = slot->offset;
            out->size = slot->size;
            out->state = GPU_BUFFER_WRITING;
            return TK2GPU_OK;
        }
        return TK2GPU_ENOSYS;

    case TK2GPU_IOCTL_FREE_BUFFER:
        if (is_bochs_qemu_vga_device(&state->device)) {
            uint32_t handle;
            bfree_gpu_buffer_slot_t *slot;
            if (!arg) return TK2GPU_EINVAL;
            handle = ((tk2gpu_buffer_t*)arg)->handle;
            slot = gpu_find_buffer_slot(handle);
            if (!slot) return TK2GPU_ENOENT;

            slot->handle = 0;
            slot->phys_addr = 0;
            slot->offset = 0;
            slot->size = 0;
            slot->in_use = 0;
            return TK2GPU_OK;
        }
        return TK2GPU_ENOSYS;

    case TK2GPU_IOCTL_SUBMIT_CMD:
        if (virtio_gpu_active()) {
            tk2gpu_command_t *cmd = (tk2gpu_command_t *)arg;
            (void)cmd;
            if (virtio_gpu_resource_flush(0, 0, 0, g_gpu_backend_state.fbinfo.width,
                                          g_gpu_backend_state.fbinfo.height) == 0) {
                return TK2GPU_OK;
            }
        }
        return TK2GPU_ENOSYS;
    case TK2GPU_IOCTL_GET_BACKEND:
        if (!arg) {
            return TK2GPU_EINVAL;
        }
        {
            tk2gpu_backend_status_t *status = (tk2gpu_backend_status_t *)arg;
            status->active_backend = state->active_backend;
            status->gpu_present = state->gpu_present;
            status->current_buffer_handle = state->current_buffer_handle;
            status->reserved = 0U;
            gpu_copy_cstr(status->backend_name, sizeof(status->backend_name), gpu_backend_active_name());
            gpu_copy_cstr(status->device_name, sizeof(status->device_name), gpu_backend_device_name());
        }
        return TK2GPU_OK;

    default:
        return TK2GPU_ENOSYS;
    }
    return TK2GPU_ENOSYS;
}