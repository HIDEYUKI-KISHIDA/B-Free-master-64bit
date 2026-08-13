/* Minimal time.h for x86_64-elf cross builds (Wayland libwayland meson checks). */
#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

typedef long time_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

int clock_gettime(int clock_id, struct timespec *tp);

#endif /* _TIME_H */
