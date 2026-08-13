/*
 * Guest ring3 fbdev for compositor.elf — maps BFREE_FB0 via B-Free syscalls
 * instead of Linux /dev/fb0 + linux/fb.h ioctls.
 *
 * Drop-in replacement for gui_server/fbdev.c when cross-building with
 * tools/compositor_guest.mk (BFREE_GUEST_COMPOSITOR=1).
 */
#include "fbdev.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bfree/bfree_guest_abi.h"

static struct {
    void *base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    int ready;
} g_fb;

static int fbdev_guest_map(void)
{
    bfree_framebuffer_info_t info;

    if (g_fb.ready)
        return 0;

    memset(&info, 0, sizeof(info));
    if (bfree_guest_syscall1(BFREE_SYS_GET_FRAMEBUFFER_INFO, (long)(uintptr_t)&info) != 0
        || !info.ready) {
        if (bfree_guest_syscall1(BFREE_SYS_GET_FRAMEBUFFER_INFO_LEGACY,
                                 (long)(uintptr_t)&info) != 0
            || !info.ready) {
            return -1;
        }
    }

    if (info.pitch == 0 || info.width == 0 || info.height == 0 || info.bpp != 32)
        return -1;

    {
        const uint64_t need = (uint64_t)info.pitch * (uint64_t)info.height;
        const long mapped = bfree_guest_syscall5(BFREE_SYS_MMAP, 0L, (long)need,
                                                 0L, 0L, (long)BFREE_FB0_FD);
        if (mapped < 0 || (uint64_t)mapped != (uint64_t)BFREE_FB0_USER_MMAP_BASE)
            return -1;
    }

    g_fb.base = (void *)(uintptr_t)BFREE_FB0_USER_MMAP_BASE;
    g_fb.width = info.width;
    g_fb.height = info.height;
    g_fb.pitch = info.pitch;
    g_fb.bpp = info.bpp;
    g_fb.ready = 1;
    return 0;
}

int fbdev_init(void)
{
    return fbdev_guest_map();
}

void *fbdev_base(void)
{
    if (!g_fb.ready && fbdev_guest_map() != 0)
        return NULL;
    return g_fb.base;
}

uint32_t fbdev_width(void)
{
    (void)fbdev_base();
    return g_fb.width;
}

uint32_t fbdev_height(void)
{
    (void)fbdev_base();
    return g_fb.height;
}

uint32_t fbdev_pitch(void)
{
    (void)fbdev_base();
    return g_fb.pitch;
}

uint32_t fbdev_bpp(void)
{
    (void)fbdev_base();
    return g_fb.bpp;
}

int fbdev_get_info(tk2gpu_fbinfo_t *out)
{
    if (!out)
        return -1;
    if (!g_fb.ready && fbdev_guest_map() != 0)
        return -1;
    memset(out, 0, sizeof(*out));
    out->width = g_fb.width;
    out->height = g_fb.height;
    out->bpp = g_fb.bpp;
    out->pitch = g_fb.pitch;
    out->size = (uint64_t)g_fb.pitch * (uint64_t)g_fb.height;
    return 0;
}

int fbdev_ioctl(int cmd, void *arg)
{
    return (int)bfree_guest_syscall3(BFREE_SYS_FBDEV_IOCTL, (long)BFREE_FB0_FD,
                                     (long)cmd, (long)(uintptr_t)arg);
}

void fbdev_present(void)
{
    /* Scanout is direct-mapped VRAM; commit paths write g_fb.base already. */
}
