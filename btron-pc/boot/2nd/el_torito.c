/* El Torito Boot Catalog生成・解析（ISO/USBブート用） */
#include "el_torito.h"
#include <string.h>

int create_el_torito_catalog(struct el_torito_boot_catalog *cat, uint32_t boot_lba, uint16_t sector_count) {
    memset(cat, 0, sizeof(*cat));
    cat->header_id = 0x01;
    cat->platform_id = 0x00;
    cat->key1 = 0x55;
    cat->key2 = 0xAA;
    cat->boot_indicator = 0x88;
    cat->media_type = 0x00;
    cat->load_segment = 0x0000;
    cat->system_type = 0x00;
    cat->sector_count = sector_count;
    cat->boot_lba = boot_lba;
    // チェックサム計算（省略）
    return 0;
}

int parse_el_torito_catalog(const unsigned char *data, struct el_torito_boot_catalog *cat) {
    memcpy(cat, data, sizeof(*cat));
    // チェックサム検証（省略）
    return 0;
}
