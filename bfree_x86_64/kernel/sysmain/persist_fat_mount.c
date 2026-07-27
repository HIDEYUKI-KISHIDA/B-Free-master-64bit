/* Freestanding FAT12/16 RO mount + single-cluster root RW for /persist. */
#include "persist_fat_mount.h"

#include <stdint.h>
#include <stddef.h>

#define BFREE_FAT_SEC 512u
#define BFREE_FAT_MAX_FAT_SECS 64u   /* ≤32KiB FAT */
#define BFREE_FAT_MAX_ROOT_SECS 32u  /* ≤512 root ents */
#define BFREE_FAT_MAX_FILE 16384u

static uint8_t g_sec[BFREE_FAT_SEC];
static uint8_t g_fat_raw[BFREE_FAT_MAX_FAT_SECS * BFREE_FAT_SEC];
static uint8_t g_root[BFREE_FAT_MAX_ROOT_SECS * BFREE_FAT_SEC];
static uint8_t g_file_buf[BFREE_FAT_MAX_FILE];

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int read_secs(bfree_persist_ata_read_fn read_sec, uint32_t lba,
                     void *dst, uint32_t nsec)
{
    uint32_t i;
    uint8_t *out = (uint8_t *)dst;
    for (i = 0; i < nsec; ++i) {
        if (read_sec(lba + i, out + i * BFREE_FAT_SEC) != 0)
            return -1;
    }
    return 0;
}

static int write_secs(bfree_persist_ata_write_fn write_sec, uint32_t lba,
                      const void *src, uint32_t nsec)
{
    uint32_t i;
    const uint8_t *in = (const uint8_t *)src;
    for (i = 0; i < nsec; ++i) {
        if (write_sec(lba + i, in + i * BFREE_FAT_SEC) != 0)
            return -1;
    }
    return 0;
}

static uint16_t fat_next(const uint8_t *fat, int fat12, uint16_t cluster)
{
    if (fat12) {
        uint32_t off = (uint32_t)cluster + (cluster / 2u);
        uint16_t val = (uint16_t)fat[off] | ((uint16_t)fat[off + 1] << 8);
        if (cluster & 1u)
            return (uint16_t)((val >> 4) & 0x0FFFu);
        return (uint16_t)(val & 0x0FFFu);
    }
    return (uint16_t)fat[(uint32_t)cluster * 2u] |
           ((uint16_t)fat[(uint32_t)cluster * 2u + 1u] << 8);
}

static void fat_set(uint8_t *fat, int fat12, uint16_t cluster, uint16_t val)
{
    if (fat12) {
        uint32_t off = (uint32_t)cluster + (cluster / 2u);
        uint16_t cur = (uint16_t)fat[off] | ((uint16_t)fat[off + 1] << 8);
        if (cluster & 1u)
            cur = (uint16_t)((cur & 0x000Fu) | ((val & 0x0FFFu) << 4));
        else
            cur = (uint16_t)((cur & 0xF000u) | (val & 0x0FFFu));
        fat[off] = (uint8_t)(cur & 0xFFu);
        fat[off + 1] = (uint8_t)((cur >> 8) & 0xFFu);
        return;
    }
    fat[(uint32_t)cluster * 2u] = (uint8_t)(val & 0xFFu);
    fat[(uint32_t)cluster * 2u + 1u] = (uint8_t)((val >> 8) & 0xFFu);
}

static int fat_is_eoc(int fat12, uint16_t c)
{
    return fat12 ? (c >= 0x0FF8u) : (c >= 0xFFF8u);
}

static void dirent_name(const uint8_t raw[32], char out[13])
{
    int i, j = 0;
    for (i = 0; i < 8 && raw[i] != ' ' && raw[i] != 0; ++i)
        out[j++] = (char)raw[i];
    if (raw[8] != ' ' && raw[8] != 0) {
        out[j++] = '.';
        for (i = 8; i < 11 && raw[i] != ' ' && raw[i] != 0; ++i)
            out[j++] = (char)raw[i];
    }
    out[j] = '\0';
}

static char up(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 'A');
    return c;
}

/* Convert "hello.txt" / "F1" / "persist/note.txt" → 11-byte 8.3. Returns 0 ok. */
static int to_83(const char *name, uint8_t out[11])
{
    const char *base = name;
    const char *dot;
    int i, n;
    const char *p;

    if (!name || !name[0])
        return -1;
    for (p = name; *p; ++p) {
        if (*p == '/')
            base = p + 1;
    }
    if (!base[0] || base[0] == '.')
        return -1;

    for (i = 0; i < 11; ++i)
        out[i] = ' ';

    dot = 0;
    for (p = base; *p; ++p) {
        if (*p == '.') {
            if (dot)
                return -1;
            dot = p;
        }
    }
    if (dot) {
        n = (int)(dot - base);
        if (n < 1 || n > 8)
            return -1;
        for (i = 0; i < n; ++i)
            out[i] = (uint8_t)up(base[i]);
        n = 0;
        for (p = dot + 1; *p && n < 3; ++p, ++n)
            out[8 + n] = (uint8_t)up(*p);
        if (*p)
            return -1;
    } else {
        n = 0;
        for (p = base; *p && n < 8; ++p, ++n)
            out[n] = (uint8_t)up(*p);
        if (*p)
            return -1;
    }
    return 0;
}

static int name83_eq(const uint8_t a[11], const uint8_t b[11])
{
    int i;
    for (i = 0; i < 11; ++i) {
        if (up((char)a[i]) != up((char)b[i]))
            return 0;
    }
    return 1;
}

static int read_cluster_chain(bfree_persist_ata_read_fn read_sec,
                              uint32_t data_lba, uint8_t spc, int fat12,
                              const uint8_t *fat, uint16_t first,
                              uint8_t *dst, uint32_t want, uint32_t *got)
{
    uint16_t c = first;
    uint32_t off = 0;
    *got = 0;
    while (c >= 2u && !fat_is_eoc(fat12, c) && off < want) {
        uint32_t lba = data_lba + (uint32_t)(c - 2u) * (uint32_t)spc;
        uint32_t s;
        for (s = 0; s < (uint32_t)spc && off < want; ++s) {
            uint32_t chunk = want - off;
            if (chunk > BFREE_FAT_SEC)
                chunk = BFREE_FAT_SEC;
            if (read_sec(lba + s, g_sec) != 0)
                return -1;
            {
                uint32_t k;
                for (k = 0; k < chunk; ++k)
                    dst[off + k] = g_sec[k];
            }
            off += chunk;
        }
        c = fat_next(fat, fat12, c);
    }
    *got = off;
    return 0;
}

struct fat_geo {
    uint8_t spc;
    uint8_t nfats;
    uint16_t reserved;
    uint16_t fat_secs;
    uint16_t root_ents;
    uint32_t root_secs;
    uint32_t root_bytes;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t data_lba;
    uint32_t clusters;
    int fat12;
};

static int parse_bpb(bfree_persist_ata_read_fn read_sec, struct fat_geo *g)
{
    uint16_t bps, total16;
    uint32_t total, data_secs;

    if (!read_sec || !g)
        return -1;
    if (read_sec(0, g_sec) != 0)
        return -1;
    if (rd16(g_sec + 510) != 0xAA55u)
        return -1;

    bps = rd16(g_sec + 11);
    g->spc = g_sec[13];
    g->reserved = rd16(g_sec + 14);
    g->nfats = g_sec[16];
    g->root_ents = rd16(g_sec + 17);
    total16 = rd16(g_sec + 19);
    g->fat_secs = rd16(g_sec + 22);
    total = total16 ? (uint32_t)total16 : rd32(g_sec + 32);

    if (bps != BFREE_FAT_SEC || g->spc == 0 || g->reserved == 0 || g->nfats == 0 ||
        g->fat_secs == 0 || g->root_ents == 0 || total == 0)
        return -1;
    if (g->fat_secs > BFREE_FAT_MAX_FAT_SECS)
        return -1;

    g->root_bytes = (uint32_t)g->root_ents * 32u;
    g->root_secs = (g->root_bytes + BFREE_FAT_SEC - 1u) / BFREE_FAT_SEC;
    if (g->root_secs > BFREE_FAT_MAX_ROOT_SECS)
        return -1;

    data_secs = total - (uint32_t)g->reserved -
                (uint32_t)g->nfats * (uint32_t)g->fat_secs - g->root_secs;
    g->clusters = data_secs / (uint32_t)g->spc;
    g->fat12 = (g->clusters < 4085u) ? 1 : 0;
    g->fat_lba = g->reserved;
    g->root_lba = g->reserved + (uint32_t)g->nfats * (uint32_t)g->fat_secs;
    g->data_lba = g->root_lba + g->root_secs;
    return 0;
}

int bfree_persist_fat_mount_ro(bfree_persist_ata_read_fn read_sec,
                               bfree_persist_fat_file_cb on_file, void *ctx)
{
    struct fat_geo g;
    uint32_t i;

    if (!read_sec || !on_file)
        return -1;
    if (parse_bpb(read_sec, &g) != 0)
        return -1;
    if (read_secs(read_sec, g.fat_lba, g_fat_raw, g.fat_secs) != 0)
        return -1;
    if (read_secs(read_sec, g.root_lba, g_root, g.root_secs) != 0)
        return -1;

    for (i = 0; i + 32u <= g.root_bytes; i += 32u) {
        const uint8_t *ent = g_root + i;
        char name[13];
        uint8_t attr;
        uint16_t first;
        uint32_t size;
        uint32_t got;

        if (ent[0] == 0x00)
            break;
        if (ent[0] == 0xE5)
            continue;
        attr = ent[11];
        if (attr & 0x08u)
            continue;
        if (attr & 0x10u)
            continue;
        if (attr & 0x0Fu)
            continue;

        dirent_name(ent, name);
        if (name[0] == '\0')
            continue;
        first = rd16(ent + 26);
        size = rd32(ent + 28);
        if (size == 0 || size > BFREE_FAT_MAX_FILE)
            continue;
        if (first < 2u)
            continue;
        if (read_cluster_chain(read_sec, g.data_lba, g.spc, g.fat12, g_fat_raw,
                               first, g_file_buf, size, &got) != 0)
            continue;
        if (got < size)
            size = got;
        if (on_file(name, g_file_buf, size, ctx) != 0)
            return -1;
    }
    return g.fat12 ? 12 : 16;
}

static uint16_t find_free_cluster(const uint8_t *fat, int fat12, uint32_t clusters)
{
    uint16_t c;
    uint16_t max = (uint16_t)(clusters + 1u);
    for (c = 2u; c <= max; ++c) {
        if (fat_next(fat, fat12, c) == 0u)
            return c;
    }
    return 0;
}

int bfree_persist_fat_put_root_file(bfree_persist_ata_read_fn read_sec,
                                    bfree_persist_ata_write_fn write_sec,
                                    const char *name83, const uint8_t *data,
                                    uint32_t size)
{
    struct fat_geo g;
    uint8_t name[11];
    uint32_t i;
    uint32_t slot = (uint32_t)-1;
    uint16_t cluster = 0;
    uint32_t cluster_bytes;
    uint32_t fi;
    uint8_t *ent;
    uint32_t lba;
    uint32_t s;
    uint32_t off;

    if (!read_sec || !write_sec || !name83)
        return -1;
    if (size > 0 && !data)
        return -1;
    if (to_83(name83, name) != 0)
        return -1;
    if (parse_bpb(read_sec, &g) != 0)
        return -1;

    cluster_bytes = (uint32_t)g.spc * BFREE_FAT_SEC;
    if (size > cluster_bytes || size > BFREE_FAT_MAX_FILE)
        return -1;

    if (read_secs(read_sec, g.fat_lba, g_fat_raw, g.fat_secs) != 0)
        return -1;
    if (read_secs(read_sec, g.root_lba, g_root, g.root_secs) != 0)
        return -1;

    for (i = 0; i + 32u <= g.root_bytes; i += 32u) {
        uint8_t *e = g_root + i;
        uint8_t attr;
        if (e[0] == 0x00) {
            if (slot == (uint32_t)-1)
                slot = i;
            break;
        }
        if (e[0] == 0xE5) {
            if (slot == (uint32_t)-1)
                slot = i;
            continue;
        }
        attr = e[11];
        if (attr & 0x0Fu)
            continue;
        if (attr & 0x08u)
            continue;
        if (attr & 0x10u)
            continue;
        if (name83_eq(e, name)) {
            slot = i;
            cluster = rd16(e + 26);
            break;
        }
    }
    if (slot == (uint32_t)-1)
        return -1;

    if (cluster < 2u) {
        cluster = find_free_cluster(g_fat_raw, g.fat12, g.clusters);
        if (cluster < 2u)
            return -1;
        fat_set(g_fat_raw, g.fat12, cluster, g.fat12 ? 0x0FFFu : 0xFFFFu);
    } else {
        /* Single-cluster v1: mark EOC; ignore any prior chain tail. */
        fat_set(g_fat_raw, g.fat12, cluster, g.fat12 ? 0x0FFFu : 0xFFFFu);
    }

    ent = g_root + slot;
    for (i = 0; i < 32u; ++i)
        ent[i] = 0;
    for (i = 0; i < 11u; ++i)
        ent[i] = name[i];
    ent[11] = 0x20; /* archive */
    wr16(ent + 26, cluster);
    wr32(ent + 28, size);

    /* Write data cluster (zero-fill remainder). */
    lba = g.data_lba + (uint32_t)(cluster - 2u) * (uint32_t)g.spc;
    for (s = 0, off = 0; s < (uint32_t)g.spc; ++s, off += BFREE_FAT_SEC) {
        uint32_t k;
        for (k = 0; k < BFREE_FAT_SEC; ++k)
            g_sec[k] = 0;
        if (off < size) {
            uint32_t chunk = size - off;
            if (chunk > BFREE_FAT_SEC)
                chunk = BFREE_FAT_SEC;
            for (k = 0; k < chunk; ++k)
                g_sec[k] = data[off + k];
        }
        if (write_sec(lba + s, g_sec) != 0)
            return -1;
    }

    /* Write both FAT copies + root. */
    for (fi = 0; fi < (uint32_t)g.nfats; ++fi) {
        if (write_secs(write_sec, g.fat_lba + fi * (uint32_t)g.fat_secs,
                       g_fat_raw, g.fat_secs) != 0)
            return -1;
    }
    if (write_secs(write_sec, g.root_lba, g_root, g.root_secs) != 0)
        return -1;
    return 0;
}
