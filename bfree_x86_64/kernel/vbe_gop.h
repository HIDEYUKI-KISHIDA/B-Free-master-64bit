// vbe_gop.h: VBE/GOP情報構造体・API定義
#ifndef VBE_GOP_H
#define VBE_GOP_H
#include <stdint.h>
#include <stddef.h>

struct vbe_info {
    uintptr_t vram_phys;
    size_t vram_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
};

void vbe_get_info(struct vbe_info *info);

/* Push authoritative FB geometry (e.g. from gpu_backend / DISPI) into vbe_gop. */
void vbe_set_info(const struct vbe_info *info);

// Multiboot2 info 構造体の type=8 タグからフレームバッファ情報を読み取り g_vbe_info を更新
void vbe_init_from_mb2(const uint8_t *mb2_info_ptr);

#endif // VBE_GOP_H
