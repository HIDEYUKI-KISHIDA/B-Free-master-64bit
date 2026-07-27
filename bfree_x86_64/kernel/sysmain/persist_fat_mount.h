/* F1c/D: FAT12/16 mount helper for /persist (ATA sector R/W). */
#ifndef BFREE_PERSIST_FAT_MOUNT_H
#define BFREE_PERSIST_FAT_MOUNT_H

#include <stddef.h>
#include <stdint.h>

typedef int (*bfree_persist_ata_read_fn)(uint32_t lba, void *buf512);
typedef int (*bfree_persist_ata_write_fn)(uint32_t lba, const void *buf512);

/* name is 8.3 short name (e.g. HELLO.TXT); return 0 on success. */
typedef int (*bfree_persist_fat_file_cb)(const char *name, const uint8_t *data,
                                        uint32_t size, void *ctx);

/* Mount FAT on LBA0 via read_sec; invoke on_file for each root file.
 * Returns 12 or 16 on success, -1 on failure. */
int bfree_persist_fat_mount_ro(bfree_persist_ata_read_fn read_sec,
                               bfree_persist_fat_file_cb on_file, void *ctx);

/* Create or overwrite one root 8.3 file (single cluster only).
 * name83 may be "HELLO.TXT" or a path basename like "note.txt" / "f1".
 * Returns 0 on success, -1 on failure. */
int bfree_persist_fat_put_root_file(bfree_persist_ata_read_fn read_sec,
                                    bfree_persist_ata_write_fn write_sec,
                                    const char *name83, const uint8_t *data,
                                    uint32_t size);

#endif
