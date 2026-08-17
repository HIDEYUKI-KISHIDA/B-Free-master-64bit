#ifndef STOS_BRINGUP_BOARD_H
#define STOS_BRINGUP_BOARD_H

#if defined(STOS_BOARD_LENA)
#include "bsp_lena_pdx213.h"
#define STOS_BANNER "STOS: lena OK\n"
#elif defined(STOS_BOARD_QEMU_VIRT)
#include "bsp_qemu_virt.h"
#define STOS_BANNER "STOS: qemu OK\n"
#else
#error "Set STOS_BOARD_QEMU_VIRT or STOS_BOARD_LENA"
#endif

#endif
