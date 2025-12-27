#ifndef __KEYBOARD_TYPE_H__
#define __KEYBOARD_TYPE_H__

// 必要に応じてダミー定義を追加
#define KEYBOARD_TYPE_DUMMY 0

#define K_101US 0
#define K_106JP 1

#define CTRL 0x01
#define SHIFT 0x02
#define ALT 0x04
#define ENCAP 0x08
extern int keyboard_type;
static unsigned char key_table_101[4][128] = {{0}};
static unsigned char key_table_106[4][128] = {{0}};

#endif
