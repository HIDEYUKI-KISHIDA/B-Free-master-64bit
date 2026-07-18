#include <stdint.h>

extern void uart_puts(const char *s);
extern void mouse_init(void);
extern void keyboard_ps2_bringup(void);
extern int gui_event_init(void) __attribute__((weak));

/*
 * Runtime subsystem hooks.
 * These strong definitions override weak stubs in subsystem.c.
 */
void input_subsystem_init(void)
{
    uart_puts("[SUBSYSTEM] input init\n");
    mouse_init();
    keyboard_ps2_bringup();
}

void timer_subsystem_init(void)
{
    uart_puts("[SUBSYSTEM] timer subsystem init\n");
}

void event_subsystem_init(void)
{
    uart_puts("[SUBSYSTEM] event bus init\n");
    if (gui_event_init) {
        if (gui_event_init() == 0) {
            uart_puts("[SUBSYSTEM] event wakeup ready\n");
        } else {
            uart_puts("[SUBSYSTEM] event wakeup init failed\n");
        }
    } else {
        uart_puts("[SUBSYSTEM] event wakeup unavailable\n");
    }
}
