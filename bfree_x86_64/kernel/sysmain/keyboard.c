#include <stdint.h>
#include "keyboard.h"
extern void event_publish_tk2_system(uintptr_t code, uintptr_t arg) __attribute__((weak));
#include "sysdepend/x86_64/uart.h"

// kputcプロトタイプ宣言
extern void kputc(char);

// --- 必須インクルード ---
#include <stdint.h>
#include "keyboard.h"
#include "sysdepend/x86_64/uart.h"

// --- グローバル状態・テーブル・マクロ・inb関数を必ずここに集約 ---
#define KBD_BUF_SIZE 128
static char kbd_buffer[KBD_BUF_SIZE];
static volatile int kbd_head = 0, kbd_tail = 0;
static int shift_pressed = 0;
static int capslock_on = 0;
static int numlock_on = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int keyboard_layout = 0; /* 0=US (QEMU default), 1=JIS */
static volatile int kbd_e0_prefix = 0;

static const char scancode_table_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=', '\b',
    '\t',
    'q', 'w', 'e', 'r',
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`',   0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.',
    '/',   0,
    '*',
    0,
    ' ',
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    '-',0,0,0,'+',0,0,0,0,0,0,0,0,0,0,0
};
static const char scancode_table_us_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b',
    '\t',
    'Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~',   0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>',
    '?',   0,
    '*',
    0,
    ' ',
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    '-',0,0,0,'+',0,0,0,0,0,0,0,0,0,0,0
};
static const char scancode_table_us_numpad[128] = {
    [0x47] = '7', [0x48] = '8', [0x49] = '9', [0x4A] = '-',
    [0x4B] = '4', [0x4C] = '5', [0x4D] = '6', [0x4E] = '+',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3',
    [0x52] = '0', [0x53] = '.'
};
static const char scancode_table_jis[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '^', '\b',
    '\t',
    'q', 'w', 'e', 'r',
    't', 'y', 'u', 'i', 'o', 'p', '@', '[', '\n',
    0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    ':', ']', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.',
    '/', 0,
    '*', 0,
    ' ', 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '-', 0,0,0,
    '+', 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static const char scancode_table_jis_shift[128] = {
    0,  27, '!', '"', '#', '$', '%', '&', '\'', '(',
    ')', '0', '=', '~', '\b',
    '\t',
    'Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '`', '{', '\n',
    0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', '+',
    '*', '}', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>',
    '?', 0,
    '*', 0,
    ' ', 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '-', 0,0,0,
    '+', 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static const char scancode_table_jis_numpad[128] = {
    [0x47] = '7', [0x48] = '8', [0x49] = '9', [0x4A] = '-',
    [0x4B] = '4', [0x4C] = '5', [0x4D] = '6', [0x4E] = '+',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3',
    [0x52] = '0', [0x53] = '.'
};

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void kb_wait_write(void)
{
    uint32_t n = 0;
    while ((inb(0x64U) & 2U) != 0U && n < 200000U) {
        ++n;
    }
}

static int kb_wait_read_byte(uint8_t *out)
{
    uint32_t n = 0;
    while ((inb(0x64U) & 1U) == 0U && n < 200000U) {
        ++n;
    }
    if ((inb(0x64U) & 1U) == 0U) {
        return -1;
    }
    *out = inb(0x60U);
    return 0;
}

static void kb_flush_output(void)
{
    while ((inb(0x64U) & 1U) != 0U) {
        (void)inb(0x60U);
    }
}

/*
 * QEMU defaults to scan code set 2; our tables are set 1 (IBM PC make/break).
 * Also ensure the first PS/2 port is enabled (0xAE) after mouse init may have toggled the 8042.
 */
void keyboard_ps2_bringup(void)
{
    uint8_t b;

    kb_flush_output();

    kb_wait_write();
    outb(0x64U, 0xAEU);
    kb_wait_write();

    kb_wait_write();
    outb(0x60U, 0xF0U);
    if (kb_wait_read_byte(&b) != 0 || b != 0xFAU) {
        uart_puts("[KBD] PS/2 set-scancode: no ACK to F0\n");
        return;
    }
    kb_wait_write();
    outb(0x60U, 0x01U);
    if (kb_wait_read_byte(&b) != 0 || b != 0xFAU) {
        uart_puts("[KBD] PS/2 set-scancode: no ACK to 01\n");
        return;
    }
    uart_puts("[KBD] PS/2 keyboard: port enabled, scan code set 1\n");
}

// --- 状態付きASCII変換（US/JIS配列切替対応） ---
char scancode_to_ascii(uint8_t sc) {
    const char *table, *table_shift, *table_numpad;
    if (sc >= 0x80U) {
        return 0;
    }
    if (keyboard_layout == 0) {
        table = scancode_table_us;
        table_shift = scancode_table_us_shift;
        table_numpad = scancode_table_us_numpad;
    } else {
        table = scancode_table_jis;
        table_shift = scancode_table_jis_shift;
        table_numpad = scancode_table_jis_numpad;
    }
    if (numlock_on && sc >= 0x47 && sc <= 0x53 && table_numpad[sc])
        return table_numpad[sc];
    if (shift_pressed) {
        return table_shift[sc];
    } else if (capslock_on && sc >= 0x1E && sc <= 0x28) {
        char c = table[sc];
        if (c >= 'a' && c <= 'z') return c - 32;
        return c;
    } else {
        return table[sc];
    }
}
// プロトタイプ宣言を追加
void keyboard_irq_handler(void *regs, int irq, unsigned long long errcode);
static void kbd_enqueue_char(char c);
void keyboard_on_ps2_data(uint8_t scancode);

// ラッパー: void *regs だけ受け取る形で本体を呼ぶ
void keyboard_irq_handler_shim(void *regs) {
    keyboard_irq_handler(regs, 1, 0);
}


// main.cから参照されるグローバルなIRQ1ハンドラ

void keyboard_irq_handler(void *regs, int irq, unsigned long long errcode) {
    (void)regs;
    (void)irq;
    (void)errcode;
    for (;;) {
        uint8_t st = inb(0x64U);
        if ((st & 1U) == 0U) {
            break;
        }
        uint8_t data = inb(0x60U);
        if ((st & 0x20U) != 0U) {
            break;
        }
        keyboard_on_ps2_data(data);
    }
    outb(0x20U, 0x20U);  /* IRQ1 EOI: master PIC only */
}

static void kbd_enqueue_char(char c)
{
    int next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail) {
        return;
    }
    kbd_buffer[kbd_head] = c;
    kbd_head = next;
    if (event_publish_tk2_system) {
        event_publish_tk2_system(1U, (uintptr_t)(unsigned char)c);
    }
}

void keyboard_on_ps2_data(uint8_t scancode)
{
    if (kbd_e0_prefix) {
        kbd_e0_prefix = 0;
        if (scancode == 0x1DU) {
            ctrl_pressed = 1;
            return;
        }
        if (scancode == (0x1DU | 0x80U)) {
            ctrl_pressed = 0;
            return;
        }
        if (scancode == 0x38U) {
            alt_pressed = 1;
            return;
        }
        if (scancode == (0x38U | 0x80U)) {
            alt_pressed = 0;
            return;
        }
        if (scancode == 0x36U) {
            shift_pressed = 1;
            return;
        }
        if (scancode == (0x36U | 0x80U)) {
            shift_pressed = 0;
            return;
        }
        if (scancode == 0x1CU) {
            kbd_enqueue_char('\n');
            return;
        }
        if (scancode == (0x1CU | 0x80U)) {
            return;
        }
        if ((scancode & 0x80U) != 0U) {
            return;
        }
        {
            char c = scancode_to_ascii(scancode);
            if (c != 0) {
                kbd_enqueue_char(c);
            }
        }
        return;
    }

    if (scancode == 0xE0U) {
        kbd_e0_prefix = 1;
        return;
    }
    if (scancode == 0xE1U) {
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }
    if (scancode == (0x2A | 0x80) || scancode == (0x36 | 0x80)) {
        shift_pressed = 0;
        return;
    }
    if (scancode == 0x3A) {
        capslock_on ^= 1;
        return;
    }
    if (scancode == 0x45) {
        numlock_on ^= 1;
        return;
    }
    if (scancode == 0x1D) {
        ctrl_pressed = 1;
        return;
    }
    if (scancode == (0x1D | 0x80)) {
        ctrl_pressed = 0;
        return;
    }
    if (scancode == 0x38) {
        alt_pressed = 1;
        return;
    }
    if (scancode == (0x38 | 0x80)) {
        alt_pressed = 0;
        return;
    }

    if (scancode & 0x80) {
        return;
    }

    /* Set 1: Return / Enter (main keyboard). Always deliver LF for login UI. */
    if (scancode == 0x1CU) {
        kbd_enqueue_char('\n');
        return;
    }

    {
        char c = scancode_to_ascii(scancode);
        if (c != 0) {
            kbd_enqueue_char(c);
        }
    }
}

void keyboard_handler(void)
{
    keyboard_irq_handler(0, 0, 0);
}

int keyboard_has_data(void) {
    return kbd_head != kbd_tail;
}

int keyboard_pop_char(uint32_t *out_char) {
    if (out_char == 0 || kbd_head == kbd_tail) {
        return 0;
    }

    *out_char = (unsigned char)kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return 1;
}


char kgetc(void) {
    while (kbd_head == kbd_tail) __asm__ volatile("hlt");
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}


char* kgets(char* buf, int size) {
    int i = 0;
    while (i < size - 1) {
        char c = kgetc();
        if (c == '\r' || c == '\n') {
            buf[i] = '\0';
            kputc('\n');
            return buf;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                kputc('\b'); // VGA上で1文字消す
            }
        } else if (c >= 32 && c <= 126) {
            buf[i++] = c;
            kputc(c);
        }
    }
    buf[i] = '\0';
    return buf;
}
