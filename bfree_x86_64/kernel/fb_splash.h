#ifndef FB_SPLASH_H
#define FB_SPLASH_H

#include <stdint.h>

void fb_draw_splash(void);
void fb_draw_splash_frame(uint32_t frame);
void fb_run_boot_splash_anim(uint32_t cycles);
/* Solid fill using current vbe_info (full pitch rows). Call before ring3 handoff. */
void fb_clear_screen(uint32_t rgb24);

#endif
