/* Minimal signalfd stub for x86_64-elf cross builds (Wayland libwayland meson check). */
#ifndef _SYS_SIGNALFD_H
#define _SYS_SIGNALFD_H

#ifndef SFD_CLOEXEC
#define SFD_CLOEXEC 0x80000
#endif

#ifndef SFD_NONBLOCK
#define SFD_NONBLOCK 0x800
#endif

#endif /* _SYS_SIGNALFD_H */
