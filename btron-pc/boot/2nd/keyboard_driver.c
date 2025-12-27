#ifndef DUMMY_DEFS_ADDED
#define DUMMY_DEFS_ADDED
typedef int size_t; typedef int ssize_t; typedef int off_t; typedef int time_t; typedef int pid_t; typedef int uid_t; typedef int gid_t; typedef int dev_t; typedef int ino_t; typedef int mode_t; typedef int nlink_t; typedef int blksize_t; typedef int blkcnt_t; typedef int sigset_t; typedef int va_list; typedef int jmp_buf[1];
#define NULL ((void*)0)
#define __attribute__(x)
#define __asm__(x)
#define __volatile__
#define __restrict
#define __inline__
#define __extension__
#define __builtin_va_list int
#define __builtin_va_start(a,b)
#define __builtin_va_end(a)
#define __builtin_va_arg(a,b) (0)
#define __builtin_offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
#include "keyboard_driver.h"
#include "lib.h"

/* キーボードバッファ */
struct kb_buffer kb_buf = {0};
unsigned char kb_state = 0;

/* ASCIIコード変換テーブル（シフトなし） */
static const char ascii_table[] = {
    0,     27,    '1',   '2',   '3',   '4',   '5',   '6',
    '7',   '8',   '9',   '0',   '-',   '=',   '\b',  '\t',
    'q',   'w',   'e',   'r',   't',   'y',   'u',   'i',
    'o',   'p',   '[',   ']',   '\n',  0,     'a',   's',
    'd',   'f',   'g',   'h',   'j',   'k',   'l',   ';',
    '\'',  '`',   0,     '\\',  'z',   'x',   'c',   'v',
    'b',   'n',   'm',   ',',   '.',   '/',   0,     '*',
    0,     ' ',   0,     0,     0,     0,     0,     0,
    0,     0,     0,     0,     0,     0,     0,     0,
    0,     0,     0,     0,     0,     0,     0,     0,
    0,     0,     0,     0,     0,     0,     0,     0,
};

/* ASCIIコード変換テーブル（シフト押下） */
static const char ascii_shift_table[] = {
    0,     27,    '!',   '@',   '#',   '$',   '%',   '^',
    '&',   '*',   '(',   ')',   '_',   '+',   '\b',  '\t',
    'Q',   'W',   'E',   'R',   'T',   'Y',   'U',   'I',
    'O',   'P',   '{',   '}',   '\n',  0,     'A',   'S',
    'D',   'F',   'G',   'H',   'J',   'K',   'L',   ':',
    '\"',  '~',   0,     '|',   'Z',   'X',   'C',   'V',
    'B',   'N',   'M',   '<',   '>',   '?',   0,     '*',
    0,     ' ',   0,     0,     0,     0,     0,     0,
    0,     0,     0,     0,     0,     0,     0,     0,
    0,     0,     0,     0,     0,     0,     0,     0,
    0,     0,     0,     0,     0,     0,     0,     0,
};

/* キーボードバッファ初期化 */
static void kb_buffer_init(void)
{
    kb_buf.head = 0;
    kb_buf.tail = 0;
    kb_buf.count = 0;
}

/* キーボードバッファが空か確認 */
static int kb_buffer_is_empty(void)
{
    return kb_buf.count == 0;
}

/* キーボードバッファが満杯か確認 */
static int kb_buffer_is_full(void)
{
    return kb_buf.count >= KB_BUFFER_SIZE;
}

/* キーボードバッファに文字を追加 */
static int kb_buffer_put(unsigned char ch)
{
    if (kb_buffer_is_full()) {
        return -1;
    }
    
    kb_buf.buffer[kb_buf.tail] = ch;
    kb_buf.tail = (kb_buf.tail + 1) % KB_BUFFER_SIZE;
    kb_buf.count++;
    
    return 0;
}

/* キーボードバッファから文字を取得 */
static int kb_buffer_get(void)
{
    int ch;
    
    if (kb_buffer_is_empty()) {
        return -1;
    }
    
    ch = kb_buf.buffer[kb_buf.head];
    kb_buf.head = (kb_buf.head + 1) % KB_BUFFER_SIZE;
    kb_buf.count--;
    
    return ch;
}

/* キーボードコントローラから1バイト受け取る（タイムアウト付き） */
static int kb_read_byte(void)
{
    int timeout = 10000;
    unsigned char status;
    
    while (timeout-- > 0) {
        status = inb(KB_CTRL_PORT);
        if (status & 0x01) {  /* バッファにデータあり */
            return inb(KB_DATA_PORT);
        }
    }
    
    return -1;  /* タイムアウト */
}

/* キーボードコントローラにコマンドを送信 */
static int kb_write_cmd(unsigned char cmd)
{
    int timeout = 10000;
    unsigned char status;
    
    while (timeout-- > 0) {
        status = inb(KB_CTRL_PORT);
        if ((status & 0x02) == 0) {  /* 入力バッファが空 */
            outb(KB_DATA_PORT, cmd);
            return 0;
        }
    }
    
    return -1;  /* タイムアウト */
}

/* キーボード初期化 */
int keyboard_init(void)
{
    kb_buffer_init();
    kb_state = 0;
    
    /* キーボード有効化 */
    keyboard_enable();
    
    console_printf("Keyboard driver initialized\n");
    
    return 0;
}

/* キーボード割り込みハンドラ */
void keyboard_handler(void)
{
    unsigned char scancode;
    unsigned char flags = 0;
    char ascii;
    
    scancode = inb(KB_DATA_PORT);
    
    /* ブレークコード確認 */
    if (scancode & 0x80) {
        /* キーリリース */
        scancode &= 0x7F;
        
        if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) {
            kb_state &= ~(KB_SHIFT_LEFT | KB_SHIFT_RIGHT);
        } else if (scancode == SC_LCTRL) {
            kb_state &= ~KB_CTRL;
        }
        
        return;
    }
    
    /* メイクコード */
    switch (scancode) {
        case SC_LSHIFT:
        case SC_RSHIFT:
            kb_state |= KB_SHIFT_LEFT;
            return;
        
        case SC_LCTRL:
            kb_state |= KB_CTRL;
            return;
        
        case SC_CAPSLOCK:
            kb_state ^= KB_CAPSLOCK;
            return;
    }
    
    /* ASCII変換 */
    flags = kb_state;
    ascii = scancode_to_ascii(scancode, flags);
    
    if (ascii != 0) {
        kb_buffer_put((unsigned char)ascii);
    }
}

/* スキャンコードをASCIIに変換 */
char scancode_to_ascii(unsigned char scancode, unsigned char flags)
{
    if (scancode >= 0x58) {  /* テーブル範囲外 */
        return 0;
    }
    
    if (flags & KB_SHIFT_LEFT) {
        return ascii_shift_table[scancode];
    } else {
        return ascii_table[scancode];
    }
}

/* 1文字入力 */
char keyboard_getchar(void)
{
    int ch;
    
    while (kb_buffer_is_empty()) {
        asm("hlt");  /* 割り込み待機 */
    }
    
    ch = kb_buffer_get();
    return (char)ch;
}

/* 入力が利用可能か確認 */
int keyboard_has_input(void)
{
    return !kb_buffer_is_empty();
}

/* 生スキャンコード読み込み */
int keyboard_read_raw_scancode(void)
{
    return kb_read_byte();
}

/* LEDを設定 */
int keyboard_set_leds(int leds)
{
    int response;
    
    if (kb_write_cmd(KB_CMD_SET_LED) != 0) {
        return -1;
    }
    
    response = kb_read_byte();
    if (response != KB_ACK) {
        return -1;
    }
    
    if (kb_write_cmd((unsigned char)leds) != 0) {
        return -1;
    }
    
    response = kb_read_byte();
    if (response != KB_ACK) {
        return -1;
    }
    
    return 0;
}

/* キーボードリセット */
int keyboard_reset(void)
{
    int response;
    
    if (kb_write_cmd(KB_CMD_RESET) != 0) {
        return -1;
    }
    
    /* BAT完了を待機 */
    response = kb_read_byte();
    if (response != 0xAA) {
        return -1;
    }
    
    return 0;
}

/* キーボード有効化 */
int keyboard_enable(void)
{
    int response;
    
    if (kb_write_cmd(KB_CMD_ENABLE) != 0) {
        return -1;
    }
    
    response = kb_read_byte();
    if (response != KB_ACK) {
        return -1;
    }
    
    return 0;
}

/* キーボード無効化 */
int keyboard_disable(void)
{
    int response;
    
    if (kb_write_cmd(KB_CMD_DISABLE) != 0) {
        return -1;
    }
    
    response = kb_read_byte();
    if (response != KB_ACK) {
        return -1;
    }
    
    return 0;
}

/* キーボード状態確認 */
int keyboard_is_shift_pressed(void)
{
    return (kb_state & KB_SHIFT_LEFT) ? 1 : 0;
}

int keyboard_is_ctrl_pressed(void)
{
    return (kb_state & KB_CTRL) ? 1 : 0;
}

int keyboard_is_alt_pressed(void)
{
    return (kb_state & KB_ALT) ? 1 : 0;
}

int keyboard_is_capslock_on(void)
{
    return (kb_state & KB_CAPSLOCK) ? 1 : 0;
}
