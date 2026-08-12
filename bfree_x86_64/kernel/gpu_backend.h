#ifndef TK2_GPU_BACKEND_H
#define TK2_GPU_BACKEND_H

#include <stdint.h>
#include <stddef.h>

/* GPU IOCTL Definitions */
#define TK2GPU_IOCTL_BASE         0x1000
#define TK2GPU_IOCTL_GET_INFO     (TK2GPU_IOCTL_BASE + 1)
#define TK2GPU_IOCTL_WAIT_VBLANK  (TK2GPU_IOCTL_BASE + 2)
#define TK2GPU_IOCTL_ALLOC_BUFFER (TK2GPU_IOCTL_BASE + 3)
#define TK2GPU_IOCTL_FREE_BUFFER  (TK2GPU_IOCTL_BASE + 4)
#define TK2GPU_IOCTL_PAGE_FLIP    (TK2GPU_IOCTL_BASE + 5)
#define TK2GPU_IOCTL_SUBMIT_CMD   (TK2GPU_IOCTL_BASE + 6)
#define TK2GPU_IOCTL_GET_BACKEND  (TK2GPU_IOCTL_BASE + 7)

/* Stable return codes for TK2GPU ioctl ABI (negative errno style). */
#define TK2GPU_OK          0
#define TK2GPU_EINVAL     -22
#define TK2GPU_ENOENT      -2
#define TK2GPU_ENOSYS     -38
#define TK2GPU_EIO         -5

/* GPU Command structure for 3D acceleration */
typedef struct {
    void*    cmd_ptr;
    size_t   size;
} tk2gpu_command_t;

typedef enum {
    GPU_BUFFER_FREE = 0,
    GPU_BUFFER_WRITING,
    GPU_BUFFER_PENDING,
    GPU_BUFFER_ACTIVE
} gpu_buffer_state_t;

typedef struct {
    uint32_t handle;
    uint64_t phys_addr;
    void*    vaddr;
    uint64_t offset;
    size_t   size;
    gpu_buffer_state_t state;
} tk2gpu_buffer_t;

/* ABI note:
 * - handle=0 is always the front buffer (scanout buffer).
 * - allocated handles (>0) represent back buffers within fbinfo.phys_addr..size.
 * - offset is byte offset from fbinfo.phys_addr.
 */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint64_t phys_addr;
    size_t   size;
} tk2gpu_fbinfo_t;

typedef struct {
    int active_backend;
    int gpu_present;
    uint32_t current_buffer_handle;
    uint32_t reserved;
    char backend_name[16];
    char device_name[32];
} tk2gpu_backend_status_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t pci_bus;
    uint8_t pci_dev;
    uint8_t pci_func;
    uintptr_t mmio_base;
    uintptr_t vram_base;
    uint32_t mmio_size;
    uint32_t vram_size;
    int irq;
} bfree_gpu_device_info_t;

typedef enum {
    BFREE_GPU_BACKEND_FRAMEBUFFER = 0,
    BFREE_GPU_BACKEND_MMIO_GPU = 1
} bfree_gpu_backend_type_t;

typedef struct bfree_gpu_backend_ops {
    int  (*init)(const bfree_gpu_device_info_t *device, tk2gpu_fbinfo_t *fbinfo);
    int  (*wait_vblank)(void);
    int  (*page_flip)(uint32_t buffer_handle);
    void (*shutdown)(void);
} bfree_gpu_backend_ops_t;

typedef struct {
    int initialized;
    int gpu_present;
    int active_backend;
    bfree_gpu_device_info_t device;
    tk2gpu_fbinfo_t fbinfo;
    const bfree_gpu_backend_ops_t *ops;
    uint32_t current_buffer_handle;
    const char *device_name;
} bfree_gpu_backend_state_t;

/* Backend API */
void gpu_backend_init(void);
int gpu_backend_alloc_buffer(tk2gpu_buffer_t *buf);
int gpu_backend_page_flip(uint32_t handle);
void gpu_backend_kick(void);
int gpu_backend_try_activate_gpu(const bfree_gpu_device_info_t *device);
int gpu_backend_active_type(void);
const char *gpu_backend_active_name(void);
const char *gpu_backend_device_name(void);
const bfree_gpu_backend_state_t *gpu_backend_state(void);
int gpu_backend_ioctl(int cmd, void *arg);

/* Cache Control Integration */
void gpu_cache_flush_buffer(void *vaddr, size_t size);

#endif /* TK2_GPU_BACKEND_H */