#ifndef __KEYBOARD_DRIVER_H__
#define __KEYBOARD_DRIVER_H__

#include "types.h"

/* キーボードコントローラポート */
#define KB_DATA_PORT    0x60
#define KB_CTRL_PORT    0x64

/* キーボードコマンド */
#define KB_CMD_RESET        0xFF
#define KB_CMD_RESEND       0xFE
#define KB_CMD_SET_LED      0xED
#define KB_CMD_ECHO         0xEE
#define KB_CMD_SCANCODE_SET 0xF0
#define KB_CMD_ENABLE       0xF4
#define KB_CMD_DISABLE      0xF5
#define KB_CMD_DEFAULT      0xF6
#define KB_CMD_ALL_TYPEMATIC 0xF7
#define KB_CMD_ALL_MAKE_BREAK 0xF8
#define KB_CMD_ALL_MAKE     0xF9
#define KB_CMD_ALL_TYPEMATIC_MAKE_BREAK 0xFA
#define KB_CMD_TYPEMATIC    0xFB

/* キーボード返答 */
#define KB_ACK          0xFA
#define KB_RESEND       0xFE
#define KB_ERROR        0xFF
#define KB_BUF_FULL     0x02
#define KB_BREAK        0x80

/* スキャンコード定義 */
#define SC_ESCAPE       0x01
#define SC_1            0x02
#define SC_2            0x03
#define SC_3            0x04
#define SC_4            0x05
#define SC_5            0x06
#define SC_6            0x07
#define SC_7            0x08
#define SC_8            0x09
#define SC_9            0x0A
#define SC_0            0x0B
#define SC_MINUS        0x0C
#define SC_EQUALS       0x0D
#define SC_BACKSPACE    0x0E
#define SC_TAB          0x0F

#define SC_Q            0x10
#define SC_W            0x11
#define SC_E            0x12
#define SC_R            0x13
#define SC_T            0x14
#define SC_Y            0x15
#define SC_U            0x16
#define SC_I            0x17
#define SC_O            0x18
#define SC_P            0x19
#define SC_LBRACKET     0x1A
#define SC_RBRACKET     0x1B
#define SC_ENTER        0x1C

#define SC_LCTRL        0x1D
#define SC_A            0x1E
#define SC_S            0x1F
#define SC_D            0x20
#define SC_F            0x21
#define SC_G            0x22
#define SC_H            0x23
#define SC_J            0x24
#define SC_K            0x25
#define SC_L            0x26
#define SC_SEMICOLON    0x27
#define SC_APOSTROPHE   0x28
#define SC_BACKTICK     0x29

#define SC_LSHIFT       0x2A
#define SC_BACKSLASH    0x2B
#define SC_Z            0x2C
#define SC_X            0x2D
#define SC_C            0x2E
#define SC_V            0x2F
#define SC_B            0x30
#define SC_N            0x31
#define SC_M            0x32
#define SC_COMMA        0x33
#define SC_PERIOD       0x34
#define SC_SLASH        0x35
#define SC_RSHIFT       0x36

#define SC_SPACE        0x39
#define SC_CAPSLOCK     0x3A

/* 機能キー */
#define SC_F1           0x3B
#define SC_F2           0x3C
#define SC_F3           0x3D
#define SC_F4           0x3E
#define SC_F5           0x3F
#define SC_F6           0x40
#define SC_F7           0x41
#define SC_F8           0x42
#define SC_F9           0x43
#define SC_F10          0x44

/* キーボード状態フラグ */
#define KB_SHIFT_LEFT   0x01
#define KB_SHIFT_RIGHT  0x02
#define KB_CTRL         0x04
#define KB_ALT          0x08
#define KB_CAPSLOCK     0x10

/* キーボード入力キュー */
#define KB_BUFFER_SIZE  256

struct kb_buffer {
    unsigned char buffer[KB_BUFFER_SIZE];
    int head;
    int tail;
    int count;
};

/* グローバル変数 */
extern struct kb_buffer kb_buf;
extern unsigned char kb_state;  /* シフト、Ctrl等の状態 */

/* キーボード初期化 */
int keyboard_init(void);

/* キーボード割り込みハンドラ */
void keyboard_handler(void);

/* キーボード入力取得 */
char keyboard_getchar(void);
int keyboard_has_input(void);
int keyboard_read_raw_scancode(void);

/* キーボード制御 */
int keyboard_set_leds(int leds);
int keyboard_reset(void);
int keyboard_enable(void);
int keyboard_disable(void);

/* ASCIIコード変換 */
char scancode_to_ascii(unsigned char scancode, unsigned char flags);

/* キーボード状態取得 */
int keyboard_is_shift_pressed(void);
int keyboard_is_ctrl_pressed(void);
int keyboard_is_alt_pressed(void);
int keyboard_is_capslock_on(void);

#endif  /* __KEYBOARD_DRIVER_H__ */
