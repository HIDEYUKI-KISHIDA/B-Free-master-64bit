/* Thin FAT BPB probe for /persist disk (scaffold; no mount yet). */
#ifndef BFREE_PERSIST_FAT_PROBE_H
#define BFREE_PERSIST_FAT_PROBE_H

#include <stddef.h>
#include <stdint.h>

/* Returns 12, 16, or 32 on valid BPB; 0 if not FAT. */
int bfree_persist_fat_probe(const uint8_t sec[512]);

#endif
