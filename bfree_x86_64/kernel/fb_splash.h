#ifndef FB_SPLASH_H
#define FB_SPLASH_H

#include <stdint.h>

void fb_draw_splash(void);
void fb_draw_splash_frame(uint32_t frame);
void fb_run_boot_splash_anim(uint32_t cycles);
void fb_draw_desktop_ready_frame(uint32_t frame);

/* Solid fill of visible framebuffer (current vbe_info pitch * height). */
void fb_clear_screen(uint32_t rgb24);

/* Wipe VRAM band (2x visible height) to remove double-buffer ghosts. */
void fb_clear_vram_all(uint32_t rgb24);

/* Compositor-first boot: bind VBE and solid-fill only (no logo / mock desktop). */
void fb_boot_compositor_handoff(void);

#endif
