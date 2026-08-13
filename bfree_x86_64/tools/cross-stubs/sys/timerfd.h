/* Minimal timerfd stub for x86_64-elf cross builds (Wayland libwayland meson check). */
#ifndef _SYS_TIMERFD_H
#define _SYS_TIMERFD_H

#ifndef TFD_CLOEXEC
#define TFD_CLOEXEC 0x80000
#endif

#ifndef TFD_NONBLOCK
#define TFD_NONBLOCK 0x800
#endif

#ifndef TFD_TIMER_ABSTIME
#define TFD_TIMER_ABSTIME (1 << 0)
#endif

#endif /* _SYS_TIMERFD_H */
