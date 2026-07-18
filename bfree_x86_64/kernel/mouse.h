#ifndef MOUSE_H
#define MOUSE_H


#include <stddef.h>
#include <stdint.h>
#include "device.h"

ssize_t mouse_read(void *dev, void *buf, size_t len);
int mouse_ioctl(void *dev, int cmd, void *arg);

/* Drain PS/2 aux bytes into the mouse ring (for ring3 poll when IRQ path is quiet). */
void mouse_poll_ps2(void);

#endif // MOUSE_H
