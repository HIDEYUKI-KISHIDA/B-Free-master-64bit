/*
 * Persistent block volume for B-Free guest FS (M1 block backend).
 *
 * Fixed 512-byte blocks in a host file.  Block 0 is the superblock; block 1
 * holds the inode table; data blocks start at BFREE_BLK_DATA_START.
 */
#ifndef BFREE_BLK_VOL_H
#define BFREE_BLK_VOL_H

#include <stddef.h>
#include <stdint.h>

#define BFREE_BLK_MAGIC      0xBFEE0001u
#define BFREE_BLK_VERSION    1u
#define BFREE_BLK_SIZE       512u
#define BFREE_BLK_MAX_BLOCKS 512u
#define BFREE_BLK_DATA_START 65u   /* block 0 super, blocks 1-64 inodes */
#define BFREE_BLK_MAX_INODES 64u
#define BFREE_BLK_MAX_FILE_BLKS 16u
#define BFREE_BLK_MAX_DIR_CHILDREN 32u

#define BFREE_DINO_FILE 1u
#define BFREE_DINO_DIR  2u
#define BFREE_DINO_FLAG_UNLINKED 1u

typedef struct bfree_blk_super {
	uint32_t magic;
	uint32_t version;
	uint32_t block_size;
	uint32_t block_count;
	uint32_t inode_count;
	uint32_t root_ino;
	uint32_t next_ino;
	uint8_t  bitmap[64]; /* 512 blocks max */
	uint8_t  reserved[BFREE_BLK_SIZE - 32u - 64u];
} bfree_blk_super_t;

typedef struct bfree_disk_inode {
	uint32_t ino;
	uint16_t type;
	uint16_t name_len;
	char     name[60];
	uint32_t parent_ino;
	uint32_t size;
	uint32_t child_inos[BFREE_BLK_MAX_DIR_CHILDREN];
	uint32_t child_count;
	uint32_t data_blks[BFREE_BLK_MAX_FILE_BLKS];
	uint32_t data_blk_count;
	uint32_t flags;
	uint8_t  reserved[12];
} bfree_disk_inode_t;

struct bfree_blk_vol {
	char     *path;
	uint32_t block_count;
	int       fd;
	int       dirty;
};

int  bfree_blk_format(const char *path, uint32_t block_count);
int  bfree_blk_open(struct bfree_blk_vol *vol, const char *path);
void bfree_blk_close(struct bfree_blk_vol *vol);

int  bfree_blk_read(struct bfree_blk_vol *vol, uint32_t blk, void *buf);
int  bfree_blk_write(struct bfree_blk_vol *vol, uint32_t blk, const void *buf);
int  bfree_blk_sync_super(struct bfree_blk_vol *vol, bfree_blk_super_t *sb);
int  bfree_blk_load_super(struct bfree_blk_vol *vol, bfree_blk_super_t *sb);

uint32_t bfree_blk_alloc(struct bfree_blk_vol *vol, bfree_blk_super_t *sb);
void     bfree_blk_free(struct bfree_blk_vol *vol, bfree_blk_super_t *sb,
			uint32_t blk);

int bfree_blk_write_inode(struct bfree_blk_vol *vol,
			  const bfree_disk_inode_t *ino);
int bfree_blk_read_inode(struct bfree_blk_vol *vol, uint32_t ino,
			 bfree_disk_inode_t *out);

#endif /* BFREE_BLK_VOL_H */
