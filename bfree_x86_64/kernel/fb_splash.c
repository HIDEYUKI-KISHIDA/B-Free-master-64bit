// fb_splash.c - B-Free OS フレームバッファ スプラッシュ描画
// カーネル起動後、ページング切替前に呼ぶこと (VRAM は boot PT で identity map 済み)

#include <stdint.h>
#include <stddef.h>
#include "vbe_gop.h"

// ---- カラー定数 (XRGB 32bit) -----------------------------------------------
#define C_BG_TOP    0x1A4A8A   // 明るめ青
#define C_BG_MID    0x2E6EB8   // 明るめミッドブルー
#define C_TASKBAR   0x222244   // 濃紺タスクバー
#define C_TASKBAR_H 0x4444AA   // タスクバー上端ライン
#define C_WIN_TITLE 0x0078D4   // Windows 風ブルー
#define C_WIN_BG    0xF0F0F0   // ライトグレー
#define C_WIN_FRAME 0x606060   // 枠線
#define C_START_BTN 0x005A9E   // スタートボタン
#define C_WHITE     0xFFFFFF
#define C_YELLOW    0xFFD700
#define C_GREEN     0x00CC44
#define C_RED       0xFF2200

// ---- 簡易 8×8 ビットマップフォント (ASCII 32-127) ---------------------------
// Public domain "8x8 font" の最小サブセット (B-Free OS v0.1 用文字のみ収録)
// 各エントリは 8 バイト、各バイトが 1 行 (MSB = 左端)
static const uint8_t s_font8[96][8] = {
    // 0x20 ' '
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // 0x21 '!'
    {0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00},
    // 0x22 '"'
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00},
    // 0x23 '#'
    {0x24,0x24,0x7E,0x24,0x7E,0x24,0x24,0x00},
    // 0x24 '$'
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},
    // 0x25 '%'
    {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00},
    // 0x26 '&'
    {0x38,0x6C,0x68,0x76,0xDC,0xCC,0x76,0x00},
    // 0x27 '''
    {0x18,0x18,0x10,0x00,0x00,0x00,0x00,0x00},
    // 0x28 '('
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    // 0x29 ')'
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    // 0x2A '*'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    // 0x2B '+'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    // 0x2C ','
    {0x00,0x00,0x00,0x00,0x18,0x18,0x10,0x00},
    // 0x2D '-'
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    // 0x2E '.'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    // 0x2F '/'
    {0x02,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    // 0x30 '0'
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    // 0x31 '1'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    // 0x32 '2'
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00},
    // 0x33 '3'
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    // 0x34 '4'
    {0x0E,0x1E,0x36,0x66,0x7F,0x06,0x06,0x00},
    // 0x35 '5'
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    // 0x36 '6'
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    // 0x37 '7'
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    // 0x38 '8'
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    // 0x39 '9'
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    // 0x3A ':'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    // 0x3B ';'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x10,0x00},
    // 0x3C '<'
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},
    // 0x3D '='
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    // 0x3E '>'
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},
    // 0x3F '?'
    {0x3C,0x66,0x06,0x1C,0x18,0x00,0x18,0x00},
    // 0x40 '@'
    {0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3C,0x00},
    // 0x41 'A'
    {0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x00},
    // 0x42 'B'
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    // 0x43 'C'
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    // 0x44 'D'
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    // 0x45 'E'
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00},
    // 0x46 'F'
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00},
    // 0x47 'G'
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00},
    // 0x48 'H'
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    // 0x49 'I'
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00},
    // 0x4A 'J'
    {0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00},
    // 0x4B 'K'
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    // 0x4C 'L'
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    // 0x4D 'M'
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    // 0x4E 'N'
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    // 0x4F 'O'
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    // 0x50 'P'
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    // 0x51 'Q'
    {0x3C,0x66,0x66,0x66,0x66,0x6E,0x3C,0x06},
    // 0x52 'R'
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},
    // 0x53 'S'
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    // 0x54 'T'
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    // 0x55 'U'
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    // 0x56 'V'
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    // 0x57 'W'
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    // 0x58 'X'
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    // 0x59 'Y'
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    // 0x5A 'Z'
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
    // 0x5B '['
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    // 0x5C '\\'
    {0x40,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
    // 0x5D ']'
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    // 0x5E '^'
    {0x10,0x38,0x6C,0x00,0x00,0x00,0x00,0x00},
    // 0x5F '_'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00},
    // 0x60 '`'
    {0x18,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    // 0x61 'a'
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},
    // 0x62 'b'
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00},
    // 0x63 'c'
    {0x00,0x00,0x3C,0x60,0x60,0x66,0x3C,0x00},
    // 0x64 'd'
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},
    // 0x65 'e'
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},
    // 0x66 'f'
    {0x1C,0x30,0x30,0x7C,0x30,0x30,0x30,0x00},
    // 0x67 'g'
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C},
    // 0x68 'h'
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},
    // 0x69 'i'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    // 0x6A 'j'
    {0x06,0x00,0x0E,0x06,0x06,0x06,0x66,0x3C},
    // 0x6B 'k'
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},
    // 0x6C 'l'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    // 0x6D 'm'
    {0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00},
    // 0x6E 'n'
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},
    // 0x6F 'o'
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    // 0x70 'p'
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},
    // 0x71 'q'
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06},
    // 0x72 'r'
    {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00},
    // 0x73 's'
    {0x00,0x00,0x3C,0x60,0x3C,0x06,0x7C,0x00},
    // 0x74 't'
    {0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00},
    // 0x75 'u'
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},
    // 0x76 'v'
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00},
    // 0x77 'w'
    {0x00,0x00,0x63,0x6B,0x7F,0x36,0x22,0x00},
    // 0x78 'x'
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},
    // 0x79 'y'
    {0x00,0x00,0x66,0x66,0x3E,0x06,0x3C,0x00},
    // 0x7A 'z'
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00},
    // 0x7B '{'
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    // 0x7C '|'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    // 0x7D '}'
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    // 0x7E '~'
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00},
    // 0x7F DEL
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

// ---- プリミティブ描画 --------------------------------------------------------
static uint8_t  *s_vram  = 0;
static uint32_t  s_pitch = 0;
static uint32_t  s_w     = 0;
static uint32_t  s_h     = 0;

static void pset(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= s_w || y >= s_h) return;
    uint32_t *p = (uint32_t *)(s_vram + y * s_pitch + x * 4);
    *p = color;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t x2 = x + w; if (x2 > s_w) x2 = s_w;
    uint32_t y2 = y + h; if (y2 > s_h) y2 = s_h;
    for (uint32_t py = y; py < y2; py++) {
        uint32_t *row = (uint32_t *)(s_vram + py * s_pitch + x * 4);
        uint32_t cnt = x2 - x;
        for (uint32_t i = 0; i < cnt; i++) row[i] = color;
    }
}

static void draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              uint32_t color, uint32_t thickness) {
    fill_rect(x,           y,           w,         thickness, color); // top
    fill_rect(x,           y+h-thickness, w,       thickness, color); // bottom
    fill_rect(x,           y,           thickness, h,         color); // left
    fill_rect(x+w-thickness, y,         thickness, h,         color); // right
}

// 文字 1 個描画 (scale=2 → 16×16px)
static void draw_char(uint32_t cx, uint32_t cy, char ch, uint32_t fg, uint32_t bg, uint32_t scale) {
    uint8_t u = (uint8_t)ch;
    if (u < 32 || u > 127) u = (uint8_t)'?';
    const uint8_t *bmp = s_font8[u - 32];
    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = bmp[row];
        for (uint32_t col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            for (uint32_t sy = 0; sy < scale; sy++)
                for (uint32_t sx = 0; sx < scale; sx++)
                    pset(cx + col*scale + sx, cy + row*scale + sy, color);
        }
    }
}

// 文字列描画 (scale=2: 16px幅 × 16px高)
static void draw_string(uint32_t x, uint32_t y, const char *s,
                        uint32_t fg, uint32_t bg, uint32_t scale) {
    while (*s) {
        draw_char(x, y, *s++, fg, bg, scale);
        x += 8 * scale;
    }
}

// ---- デスクトップ描画 -------------------------------------------------------
// 背景グラデーション (上→下で明青→ミッドブルー)
static void draw_background(void) {
    for (uint32_t y = 0; y < s_h; y++) {
        // 0..s_h の進行で C_BG_TOP → C_BG_MID をリニア補間
        uint32_t t   = y * 255 / (s_h ? s_h : 1);
        uint32_t inv = 255 - t;
        uint8_t r = (uint8_t)(((C_BG_TOP >> 16) & 0xFF) * inv / 255
                            + ((C_BG_MID >> 16) & 0xFF) * t   / 255);
        uint8_t g = (uint8_t)(((C_BG_TOP >>  8) & 0xFF) * inv / 255
                            + ((C_BG_MID >>  8) & 0xFF) * t   / 255);
        uint8_t b = (uint8_t)(((C_BG_TOP      ) & 0xFF) * inv / 255
                            + ((C_BG_MID      ) & 0xFF) * t   / 255);
        uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        uint32_t *row = (uint32_t *)(s_vram + y * s_pitch);
        for (uint32_t x = 0; x < s_w; x++) row[x] = color;
    }
}

// タスクバー
static void draw_taskbar(uint32_t sec) {
    uint32_t tbH = 36;
    uint32_t tbY = s_h - tbH;

    fill_rect(0, tbY, s_w, tbH, C_TASKBAR);
    fill_rect(0, tbY, s_w, 1,   C_TASKBAR_H); // 上端ハイライトライン

    // スタートボタン (左端)
    fill_rect(4, tbY + 4, 80, tbH - 8, C_START_BTN);
    draw_rect_outline(4, tbY + 4, 80, tbH - 8, C_WHITE, 1);
    draw_string(10, tbY + 10, "B-Free", C_WHITE, C_START_BTN, 1);

    // 時刻表示風テキスト (右端)
    {
        uint32_t mm = (sec / 60U) % 100U;
        uint32_t ss = sec % 60U;
        char clock[6];
        clock[0] = (char)('0' + (mm / 10U));
        clock[1] = (char)('0' + (mm % 10U));
        clock[2] = ':';
        clock[3] = (char)('0' + (ss / 10U));
        clock[4] = (char)('0' + (ss % 10U));
        clock[5] = '\0';
        draw_string(s_w - 90, tbY + 10, clock, C_WHITE, C_TASKBAR, 1);
    }
}

// メインウィンドウ
static void draw_window(uint32_t progress, uint32_t phase, uint32_t beat) {
    uint32_t wW = 480, wH = 260;
    uint32_t wX = (s_w - wW) / 2;
    uint32_t wY = (s_h - wH) / 2 - 16;
    uint32_t titleH = 28;

    // ウィンドウ影
    fill_rect(wX + 4, wY + 4, wW, wH, 0x000000);

    // ウィンドウ背景
    fill_rect(wX, wY, wW, wH, C_WIN_BG);
    draw_rect_outline(wX, wY, wW, wH, C_WIN_FRAME, 2);

    // タイトルバー
    fill_rect(wX + 2, wY + 2, wW - 4, titleH, C_WIN_TITLE);
    draw_string(wX + 8, wY + 7, "B-Free OS", C_WHITE, C_WIN_TITLE, 1);

    // 閉じるボタン
    fill_rect(wX + wW - 22, wY + 4, 18, titleH - 4, C_RED);
    draw_string(wX + wW - 18, wY + 8, "x", C_WHITE, C_RED, 1);

    // コンテンツ領域
    uint32_t cy = wY + titleH + 16;
    uint32_t cx = wX + 20;

    // ロゴ (3色ブロック)
    fill_rect(cx,      cy,      24, 24, C_WIN_TITLE);
    fill_rect(cx + 26, cy,      24, 24, C_GREEN);
    fill_rect(cx + 52, cy,      24, 24, C_YELLOW);

    // テキスト
    draw_string(cx, cy + 32,  "B-Free OS v0.1 (x86_64)", 0x111111, C_WIN_BG, 1);
    draw_string(cx, cy + 48,  "Kernel booted successfully.", 0x333333, C_WIN_BG, 1);
    {
        static const char spin[4] = {'|', '/', '-', '\\'};
        char status[48] = "Loading modules [ ] 000%";
        status[17] = spin[phase & 3U];
        status[20] = (char)('0' + (progress / 100U));
        status[21] = (char)('0' + ((progress / 10U) % 10U));
        status[22] = (char)('0' + (progress % 10U));
        draw_string(cx, cy + 68, status, 0x333333, C_WIN_BG, 1);
    }

    // ステータスバー (ウィンドウ下端)
    uint32_t sbY = wY + wH - 24;
    fill_rect(wX + 2, sbY, wW - 4, 22, 0xD0D0D0);
    draw_rect_outline(wX + 2, sbY, wW - 4, 22, 0xA0A0A0, 1);
    draw_string(wX + 8, sbY + 6, beat ? "Working..." : "Please wait...", 0x444444, 0xD0D0D0, 1);

    // プログレスバー
    uint32_t pbX = cx, pbY = cy + 90, pbW = wW - 60, pbH = 12;
    fill_rect(pbX, pbY, pbW,       pbH, 0xCCCCCC);
    fill_rect(pbX, pbY, (pbW * progress) / 100U, pbH, C_GREEN);
    draw_rect_outline(pbX, pbY, pbW, pbH, 0x888888, 1);
}

/*
 * One-shot splash before CR3 switch: must not look like a stuck progress bar.
 * (draw_window(75,…) left the bar at ~75% while serial already reached bfree-shell>.)
 */
static void draw_splash_handoff_panel(void)
{
    uint32_t wW = 480, wH = 260;
    uint32_t wX = (s_w - wW) / 2;
    uint32_t wY = (s_h - wH) / 2 - 16;
    uint32_t titleH = 28;

    fill_rect(wX + 4, wY + 4, wW, wH, 0x000000);
    fill_rect(wX, wY, wW, wH, C_WIN_BG);
    draw_rect_outline(wX, wY, wW, wH, C_WIN_FRAME, 2);

    fill_rect(wX + 2, wY + 2, wW - 4, titleH, C_WIN_TITLE);
    draw_string(wX + 8, wY + 7, "B-Free OS", C_WHITE, C_WIN_TITLE, 1);

    fill_rect(wX + wW - 22, wY + 4, 18, titleH - 4, C_RED);
    draw_string(wX + wW - 18, wY + 8, "x", C_WHITE, C_RED, 1);

    {
        uint32_t cy = wY + titleH + 16;
        uint32_t cx = wX + 20;

        fill_rect(cx, cy, 24, 24, C_WIN_TITLE);
        fill_rect(cx + 26, cy, 24, 24, C_GREEN);
        fill_rect(cx + 52, cy, 24, 24, C_YELLOW);

        draw_string(cx, cy + 32, "B-Free OS v0.1 (x86_64)", 0x111111, C_WIN_BG, 1);
        draw_string(cx, cy + 48, "Kernel booted successfully.", 0x333333, C_WIN_BG, 1);
        draw_string(cx, cy + 64, "Starting ring-3 (serial = shell / init).", 0x333333, C_WIN_BG, 1);
        draw_string(cx, cy + 80, "FB static until userland (e.g. init.elf).", 0x333333, C_WIN_BG, 1);
    }

    {
        uint32_t sbY = wY + wH - 24;
        fill_rect(wX + 2, sbY, wW - 4, 22, 0xD0D0D0);
        draw_rect_outline(wX + 2, sbY, wW - 4, 22, 0xA0A0A0, 1);
        draw_string(wX + 8, sbY + 6, "Handoff complete - not frozen", 0x444444, 0xD0D0D0, 1);
    }

    {
        uint32_t cx = wX + 20;
        uint32_t cy = wY + titleH + 16;
        uint32_t pbX = cx, pbY = cy + 100, pbW = wW - 60, pbH = 12;
        fill_rect(pbX, pbY, pbW, pbH, 0xCCCCCC);
        fill_rect(pbX, pbY, pbW, pbH, C_GREEN);
        draw_rect_outline(pbX, pbY, pbW, pbH, 0x888888, 1);
    }
}

// デスクトップ遷移後のシンプル画面（進捗バーなし）
static void draw_desktop_ready_panel(uint32_t phase) {
    uint32_t wW = 520, wH = 220;
    uint32_t wX = (s_w - wW) / 2;
    uint32_t wY = (s_h - wH) / 2 - 10;
    uint32_t titleH = 28;

    fill_rect(wX + 4, wY + 4, wW, wH, 0x000000);
    fill_rect(wX, wY, wW, wH, C_WIN_BG);
    draw_rect_outline(wX, wY, wW, wH, C_WIN_FRAME, 2);

    fill_rect(wX + 2, wY + 2, wW - 4, titleH, C_WIN_TITLE);
    draw_string(wX + 8, wY + 7, "B-Free Desktop (Mock)", C_WHITE, C_WIN_TITLE, 1);

    {
        static const char spin[4] = {'|', '/', '-', '\\'};
        char line1[40] = "Kernel hold mode: ACTIVE [ ]";
        line1[24] = spin[phase & 3U];
        draw_string(wX + 24, wY + 56, line1, 0x222222, C_WIN_BG, 1);
    }

    draw_string(wX + 24, wY + 78, "Userland handoff is currently paused.", 0x333333, C_WIN_BG, 1);
    draw_string(wX + 24, wY + 100, "This is a visual placeholder scene.", 0x333333, C_WIN_BG, 1);

    // デスクトップアイコン風
    fill_rect(wX + 28,  wY + 132, 20, 20, 0x4A90E2);
    draw_string(wX + 52, wY + 138, "Terminal (demo)", 0x222222, C_WIN_BG, 1);
    fill_rect(wX + 190, wY + 132, 20, 20, 0x7ED321);
    draw_string(wX + 214, wY + 138, "Files (demo)", 0x222222, C_WIN_BG, 1);
    fill_rect(wX + 310, wY + 132, 20, 20, 0xF5A623);
    draw_string(wX + 334, wY + 138, "Settings (demo)", 0x222222, C_WIN_BG, 1);
}

// ---- userland 起動後の確定画面 (デスクトップ風) ----------------------------
static void draw_desktop_icon(uint32_t x, uint32_t y, uint32_t color,
                               const char *label) {
    // 48x48 アイコン本体
    fill_rect(x, y, 48, 48, color);
    draw_rect_outline(x, y, 48, 48, 0xFFFFFF, 1);
    // ラベル (アイコン下8px)
    draw_string(x, y + 52, label, C_WHITE, 0x00000000, 1);
}

static void draw_userland_active_panel(void) {
    uint32_t tbH = 28;
    uint32_t deskH = s_h - tbH; // デスクトップ有効高さ

    // ---- 左端アイコン列 (x=24, 縦に並べる) ----------------------------------
    uint32_t ix = 24;
    uint32_t iy = 24;
    uint32_t step = 80; // アイコン間隔

    draw_desktop_icon(ix, iy,            0x4A90E2, "Terminal");
    draw_desktop_icon(ix, iy + step,     0x7ED321, "Files");
    draw_desktop_icon(ix, iy + step * 2, 0xF5A623, "Settings");
    draw_desktop_icon(ix, iy + step * 3, 0x9B59B6, "Browser");
    draw_desktop_icon(ix, iy + step * 4, 0xE74C3C, "Editor");

    // ---- 右下コーナー: ステータスノーティフィケーション ---------------------
    uint32_t nW = 300, nH = 100;
    uint32_t nX = s_w - nW - 12;
    uint32_t nY = deskH - nH - 8;

    fill_rect(nX + 3, nY + 3, nW, nH, 0x00000080); // 影
    fill_rect(nX, nY, nW, nH, 0x1A1A2E);
    draw_rect_outline(nX, nY, nW, nH, C_GREEN, 1);

    draw_string(nX + 10, nY + 10, "B-Free OS ready", C_GREEN, 0x1A1A2E, 1);
    draw_string(nX + 10, nY + 28, "ring3 / CPL=3 active", C_WHITE, 0x1A1A2E, 1);
    draw_string(nX + 10, nY + 46, "arch: x86_64", 0xAAAAAA, 0x1A1A2E, 1);
    draw_string(nX + 10, nY + 64, "kernel: B-Free v0.1", 0xAAAAAA, 0x1A1A2E, 1);

    // ---- 上部メニューバー ---------------------------------------------------
    fill_rect(0, 0, s_w, 22, 0x18182A);
    draw_rect_outline(0, 0, s_w, 22, 0x444466, 1);
    draw_string(8,  6, "Applications", C_WHITE, 0x18182A, 1);
    draw_string(120, 6, "Places", C_WHITE, 0x18182A, 1);
    draw_string(192, 6, "System", C_WHITE, 0x18182A, 1);

    (void)deskH;
}

void fb_draw_userland_active(void) {
    struct vbe_info vi;
    vbe_get_info(&vi);
    if (vi.vram_phys == 0 || vi.width == 0 || vi.height == 0) return;

    s_vram  = (uint8_t *)(uintptr_t)vi.vram_phys;
    s_pitch = vi.pitch;
    s_w     = vi.width;
    s_h     = vi.height;

    draw_background();
    draw_taskbar(0);
    draw_userland_active_panel();
}

// ---- 公開 API ---------------------------------------------------------------
void fb_draw_splash(void) {
    struct vbe_info vi;
    vbe_get_info(&vi);

    if (vi.vram_phys == 0 || vi.width == 0 || vi.height == 0) return;

    s_vram  = (uint8_t *)(uintptr_t)vi.vram_phys;
    s_pitch = vi.pitch;
    s_w     = vi.width;
    s_h     = vi.height;

    draw_background();
    draw_taskbar(0);
    draw_splash_handoff_panel();
}

void fb_draw_splash_frame(uint32_t frame) {
    struct vbe_info vi;
    vbe_get_info(&vi);
    if (vi.vram_phys == 0 || vi.width == 0 || vi.height == 0) return;

    s_vram  = (uint8_t *)(uintptr_t)vi.vram_phys;
    s_pitch = vi.pitch;
    s_w     = vi.width;
    s_h     = vi.height;

    // 0..100 の単調増加ループ（オーバーランなし）
    uint32_t progress = frame % 101U;
    uint32_t sec = frame / 8U;
    uint32_t phase = (frame / 8U) % 4U;
    uint32_t beat = (frame / 12U) & 1U;

    draw_background();
    draw_taskbar(sec);
    draw_window(progress, phase, beat);
}

void fb_draw_desktop_ready_frame(uint32_t frame) {
    struct vbe_info vi;
    vbe_get_info(&vi);
    if (vi.vram_phys == 0 || vi.width == 0 || vi.height == 0) return;

    s_vram  = (uint8_t *)(uintptr_t)vi.vram_phys;
    s_pitch = vi.pitch;
    s_w     = vi.width;
    s_h     = vi.height;

    uint32_t sec = frame / 8U;
    uint32_t phase = (frame / 8U) % 4U;

    draw_background();
    draw_taskbar(sec);
    draw_desktop_ready_panel(phase);
}
