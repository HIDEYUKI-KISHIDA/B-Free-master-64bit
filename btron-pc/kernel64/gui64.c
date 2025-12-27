// gui64.c - 64ビット用 シンプルなGUI/グラフィックAPI雛形（ダミー実装）
#include "../include64/types.h"

#define FB_WIDTH64  320
#define FB_HEIGHT64 200
static unsigned int framebuffer64[FB_WIDTH64 * FB_HEIGHT64];

void gui_clear64(unsigned int color) {
    for (int i = 0; i < FB_WIDTH64 * FB_HEIGHT64; i++) framebuffer64[i] = color;
}

void gui_draw_pixel64(int x, int y, unsigned int color) {
    if (x >= 0 && x < FB_WIDTH64 && y >= 0 && y < FB_HEIGHT64)
        framebuffer64[y * FB_WIDTH64 + x] = color;
}

void gui_draw_rect64(int x, int y, int w, int h, unsigned int color) {
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++)
            gui_draw_pixel64(x + j, y + i, color);
}

void gui_draw_text64(int x, int y, const char *text, unsigned int color) {
    // ダミー: 何もしない
}

void gui_show64(void) {
    // 本来はVGA/フレームバッファへ転送
}
