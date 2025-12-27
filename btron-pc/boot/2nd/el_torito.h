/* El Torito Boot Record 定義（ISO/USBブート用） */
#ifndef EL_TORITO_H
#define EL_TORITO_H

#include <stdint.h>

struct el_torito_boot_catalog {
    uint8_t header_id;      // 0x01
    uint8_t platform_id;    // 0x00: x86
    uint16_t reserved;
    uint32_t manufacturer;
    uint8_t checksum;
    uint8_t key1;
    uint8_t key2;
    uint8_t reserved2[20];
    // ...
    uint8_t boot_indicator; // 0x88: bootable
    uint8_t media_type;     // 0x00: no emulation
    uint16_t load_segment;  // 0x0000
    uint8_t system_type;    // 0x00
    uint8_t unused;
    uint16_t sector_count;  // boot image sectors
    uint32_t boot_lba;      // boot image LBA
    // ...
};

#endif
