#include "persist_fat_probe.h"

/* Freestanding BPB sniff (offsets match legacy_dosfs / FAT spec). */
int bfree_persist_fat_probe(const uint8_t sec[512])
{
    uint16_t sig;
    uint16_t bps;
    uint8_t spc;
    uint16_t reserved;
    uint8_t nfats;
    uint16_t root_ents;
    uint16_t fat16_secs;
    uint32_t fat32_secs;
    uint32_t total16;
    uint32_t total32;
    uint32_t total;
    uint32_t root_secs;
    uint32_t data_secs;
    uint32_t clusters;

    if (!sec)
        return 0;
    sig = (uint16_t)sec[510] | ((uint16_t)sec[511] << 8);
    if (sig != 0xAA55u)
        return 0;

    bps = (uint16_t)sec[11] | ((uint16_t)sec[12] << 8);
    spc = sec[13];
    reserved = (uint16_t)sec[14] | ((uint16_t)sec[15] << 8);
    nfats = sec[16];
    root_ents = (uint16_t)sec[17] | ((uint16_t)sec[18] << 8);
    total16 = (uint32_t)sec[19] | ((uint32_t)sec[20] << 8);
    fat16_secs = (uint16_t)sec[22] | ((uint16_t)sec[23] << 8);
    total32 = (uint32_t)sec[32] | ((uint32_t)sec[33] << 8) |
              ((uint32_t)sec[34] << 16) | ((uint32_t)sec[35] << 24);
    fat32_secs = (uint32_t)sec[36] | ((uint32_t)sec[37] << 8) |
                 ((uint32_t)sec[38] << 16) | ((uint32_t)sec[39] << 24);

    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096)
        return 0;
    if (spc == 0 || (spc & (spc - 1u)) != 0)
        return 0;
    if (reserved == 0 || nfats == 0)
        return 0;

    total = total16 ? total16 : total32;
    if (total == 0)
        return 0;

    if (fat16_secs == 0 && fat32_secs != 0 && root_ents == 0)
        return 32; /* FAT32 scaffold recognition only */

    if (fat16_secs == 0)
        return 0;

    root_secs = ((uint32_t)root_ents * 32u + (uint32_t)bps - 1u) / (uint32_t)bps;
    data_secs = total - (uint32_t)reserved -
                (uint32_t)nfats * (uint32_t)fat16_secs - root_secs;
    clusters = data_secs / (uint32_t)spc;
    if (clusters < 4085u)
        return 12;
    return 16;
}
