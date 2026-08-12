#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int guest_splash_arm(void);
void guest_splash_show(unsigned frame);
void guest_splash_advance(void);
int guest_splash_ready(void);
void guest_splash_disable(void);
#ifdef __cplusplus
}
#endif
