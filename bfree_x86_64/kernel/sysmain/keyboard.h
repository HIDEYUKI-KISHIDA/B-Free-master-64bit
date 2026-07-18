#pragma once
#include <stdint.h>

void keyboard_handler(void);
/* PS/2 data port byte (IRQ1 or shared PS/2 poll): enqueue translated key if any */
void keyboard_on_ps2_data(uint8_t scancode);
/* After mouse PS/2 init: enable keyboard port + scan code set 1 (matches scancode tables). */
void keyboard_ps2_bringup(void);
int keyboard_has_data(void);
int keyboard_pop_char(uint32_t *out_char);
char kgetc(void);
char* kgets(char* buf, int size);
