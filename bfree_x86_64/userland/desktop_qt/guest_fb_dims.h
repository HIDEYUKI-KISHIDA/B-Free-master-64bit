#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime FB geometry (defaults 1920x1080 until guest_splash_arm probes kernel). */
unsigned guest_fb_w(void);
unsigned guest_fb_h(void);
unsigned guest_fb_pitch(void);
int guest_fb_ready(void);

#ifdef __cplusplus
}
#endif
