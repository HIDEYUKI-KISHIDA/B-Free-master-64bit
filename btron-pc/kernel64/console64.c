void console_clear64(void) {
    for (int row = 0; row < 25; row++) {
        for (int col = 0; col < 80; col++) {
            int pos = row * 80 + col;
            VGA_TEXT_BUF[pos] = (unsigned short)' ' | 0x0700;
        }
    }
    vga_row = 0;
    vga_col = 0;
}
// console64.c - 64ビット用コンソールI/O雛形
#include "../include64/types.h"


#define VGA_TEXT_BUF ((volatile unsigned short*)0xB8000)
static int vga_row = 0, vga_col = 0;

void console_init64(void) {
    vga_row = 0;
    vga_col = 0;
}

void console_putc64(char c) {
    if (c == '\n') {
        vga_row++;
        vga_col = 0;
        return;
    }
    int pos = vga_row * 80 + vga_col;
    VGA_TEXT_BUF[pos] = (unsigned short)c | 0x0700;
    vga_col++;
    if (vga_col >= 80) {
        vga_col = 0;
        vga_row++;
    }
}

char console_getc64(void) {
    // 入力未実装（今後拡張）
    return 0;
}

// --- 追加: シェルUI拡張の雰囲気（ダミーAPI） ---

#define SHELL_HISTORY_SIZE 8
static char shell_history[SHELL_HISTORY_SIZE][128];
static int shell_hist_count = 0;

void shell_add_history64(const char *cmd) {
    if (shell_hist_count < SHELL_HISTORY_SIZE) {
        strncpy(shell_history[shell_hist_count], cmd, 127);
        shell_history[shell_hist_count][127] = 0;
        shell_hist_count++;
    }
}

const char* shell_get_history64(int idx) {
    if (idx >= 0 && idx < shell_hist_count) return shell_history[idx];
    return NULL;
}

// 色付き出力（ダミー: 色は無効）
void puts_color64(const char *s, int color) {
    // 本来はVGA属性を使う
    while (*s) console_putc64(*s++);
}

// タブ補完（ダミー: 何もしない）
void shell_tab_complete64(char *cmd) {
    // 本来はコマンド候補を補完
}

// --- ここまで追加 ---
}
