/* Minimal eventfd stub for x86_64-elf cross builds (Wayland / Qt meson checks). */
#ifndef _SYS_EVENTFD_H
#define _SYS_EVENTFD_H

#ifndef EFD_CLOEXEC
#define EFD_CLOEXEC 0x80000
#endif

#ifndef EFD_NONBLOCK
#define EFD_NONBLOCK 0x800
#endif

#ifndef EFD_SEMAPHORE
#define EFD_SEMAPHORE 1
#endif

#endif /* _SYS_EVENTFD_H */
