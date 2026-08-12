// B-Free x86_64 kernel: VBE/GOP抽象化層（雛形）
// ファイル: vbe_gop.c
// 目的: BIOS/UEFIからVBE/GOP情報を取得し、fbdev.cへ提供

#include <stdint.h>
#include <stddef.h>
#include "vbe_gop.h"

// Multiboot2 framebuffer 情報タグ (type=8) 構造体
typedef struct {
    uint32_t type;               // = 8
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;   // 0=indexed 1=RGB 2=EGA-text
    uint16_t reserved;
} __attribute__((packed)) mb2_tag_fb_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_hdr_t;

// 内部状態: 現在の VBE/MB2 フレームバッファ情報
static struct vbe_info g_vbe_info = {
    .vram_phys = 0xE0000000, // QEMU stdvga BAR0 デフォルト値
    .vram_size = 8 * 1024 * 1024,
    .width     = 1920,
    .height    = 1080,
    .pitch     = 1920 * 4,
    .bpp       = 32,
};

// Multiboot2 info struct から framebuffer タグ (type=8) をパースして g_vbe_info を更新
void vbe_init_from_mb2(const uint8_t *mb2_info_ptr)
{
    if (!mb2_info_ptr) return;

    // MB2 info 先頭 8 バイト: total_size(u32) + reserved(u32)
    uint32_t total_size = *(const uint32_t *)mb2_info_ptr;
    const uint8_t *ptr = mb2_info_ptr + 8;
    const uint8_t *end = mb2_info_ptr + total_size;

    while (ptr < end) {
        const mb2_tag_hdr_t *hdr = (const mb2_tag_hdr_t *)ptr;
        if (hdr->type == 0) break; // terminator
        if (hdr->size < 8) break;  // sanity

        if (hdr->type == 8 && hdr->size >= sizeof(mb2_tag_fb_t)) {
            const mb2_tag_fb_t *fb = (const mb2_tag_fb_t *)ptr;
            // EGA テキストモードは無視
            if (fb->framebuffer_type != 2 &&
                fb->framebuffer_addr  != 0 &&
                fb->framebuffer_width  != 0 &&
                fb->framebuffer_height != 0) {
                g_vbe_info.vram_phys = (uintptr_t)fb->framebuffer_addr;
                g_vbe_info.vram_size = (size_t)fb->framebuffer_pitch
                                     * fb->framebuffer_height;
                g_vbe_info.width     = fb->framebuffer_width;
                g_vbe_info.height    = fb->framebuffer_height;
                g_vbe_info.pitch     = fb->framebuffer_pitch;
                g_vbe_info.bpp       = fb->framebuffer_bpp;
            }
            break;
        }
        // 次のタグへ (8 バイトアライン)
        ptr += (hdr->size + 7) & ~7U;
    }
}

// 現在の g_vbe_info を返す
void vbe_get_info(struct vbe_info *info) {
    if (!info) return;
    *info = g_vbe_info;
}

// 本来はUEFI/BIOSから情報取得する処理を実装
