/*
 * Block volume: format, bitmap allocator, inode table I/O.
 */
#include "blk_vol.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *path_dup(const char *path)
{
	size_t len;
	char *copy;

	if (path == NULL)
		return NULL;
	len = strlen(path);
	copy = malloc(len + 1);
	if (copy == NULL)
		return NULL;
	memcpy(copy, path, len + 1);
	return copy;
}

static int bitmap_test(const uint8_t *bm, uint32_t blk)
{
	if (blk >= BFREE_BLK_MAX_BLOCKS)
		return 0;
	return (bm[blk / 8] >> (blk % 8)) & 1;
}

static void bitmap_set(uint8_t *bm, uint32_t blk)
{
	if (blk < BFREE_BLK_MAX_BLOCKS)
		bm[blk / 8] |= (uint8_t)(1u << (blk % 8));
}

static void bitmap_clear(uint8_t *bm, uint32_t blk)
{
	if (blk < BFREE_BLK_MAX_BLOCKS)
		bm[blk / 8] &= (uint8_t)~(1u << (blk % 8));
}

static uint32_t inode_table_blk(uint32_t ino)
{
	return 1u + (ino - 1u);
}

int bfree_blk_format(const char *path, uint32_t block_count)
{
	struct bfree_blk_vol vol;
	bfree_blk_super_t sb;
	uint32_t i;
	uint8_t zero[BFREE_BLK_SIZE];
	int fd;

	if (block_count < BFREE_BLK_DATA_START + 4 ||
	    block_count > BFREE_BLK_MAX_BLOCKS)
		return -EINVAL;

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -errno;

	memset(&vol, 0, sizeof(vol));
	vol.path = path_dup(path);
	vol.fd = fd;
	vol.block_count = block_count;

	memset(&sb, 0, sizeof(sb));
	sb.magic = BFREE_BLK_MAGIC;
	sb.version = BFREE_BLK_VERSION;
	sb.block_size = BFREE_BLK_SIZE;
	sb.block_count = block_count;
	sb.inode_count = 1;
	sb.root_ino = 1;
	sb.next_ino = 2;
	for (i = 0; i < BFREE_BLK_DATA_START; i++)
		bitmap_set(sb.bitmap, i);

	memset(zero, 0, sizeof(zero));
	for (i = 0; i < block_count; i++) {
		if (bfree_blk_write(&vol, i, zero) < 0) {
			bfree_blk_close(&vol);
			return -EIO;
		}
	}

	if (bfree_blk_sync_super(&vol, &sb) < 0) {
		bfree_blk_close(&vol);
		return -EIO;
	}

	{
		bfree_disk_inode_t root;

		memset(&root, 0, sizeof(root));
		root.ino = 1;
		root.type = BFREE_DINO_DIR;
		root.name_len = 1;
		snprintf(root.name, sizeof(root.name), "/");
		root.parent_ino = 0;
		if (bfree_blk_write_inode(&vol, &root) < 0) {
			bfree_blk_close(&vol);
			return -EIO;
		}
	}

	bfree_blk_close(&vol);
	return 0;
}

int bfree_blk_open(struct bfree_blk_vol *vol, const char *path)
{
	bfree_blk_super_t sb;

	if (vol == NULL || path == NULL)
		return -EINVAL;

	memset(vol, 0, sizeof(*vol));
	vol->path = path_dup(path);
	if (vol->path == NULL)
		return -ENOMEM;

	vol->fd = open(path, O_RDWR);
	if (vol->fd < 0) {
		free(vol->path);
		vol->path = NULL;
		return -errno;
	}

	if (bfree_blk_load_super(vol, &sb) < 0) {
		close(vol->fd);
		free(vol->path);
		vol->path = NULL;
		vol->fd = -1;
		return -EINVAL;
	}
	vol->block_count = sb.block_count;
	return 0;
}

void bfree_blk_close(struct bfree_blk_vol *vol)
{
	if (vol == NULL)
		return;
	if (vol->fd >= 0)
		close(vol->fd);
	free(vol->path);
	memset(vol, 0, sizeof(*vol));
	vol->fd = -1;
}

int bfree_blk_read(struct bfree_blk_vol *vol, uint32_t blk, void *buf)
{
	off_t off;

	if (vol == NULL || buf == NULL)
		return -EINVAL;
	if (vol->block_count > 0 && blk >= vol->block_count)
		return -EINVAL;
	off = (off_t)blk * (off_t)BFREE_BLK_SIZE;
	if (lseek(vol->fd, off, SEEK_SET) < 0)
		return -errno;
	if (read(vol->fd, buf, BFREE_BLK_SIZE) != (ssize_t)BFREE_BLK_SIZE)
		return -EIO;
	return 0;
}

int bfree_blk_write(struct bfree_blk_vol *vol, uint32_t blk, const void *buf)
{
	off_t off;

	if (vol == NULL || buf == NULL)
		return -EINVAL;
	if (vol->block_count > 0 && blk >= vol->block_count)
		return -EINVAL;
	off = (off_t)blk * (off_t)BFREE_BLK_SIZE;
	if (lseek(vol->fd, off, SEEK_SET) < 0)
		return -errno;
	if (write(vol->fd, buf, BFREE_BLK_SIZE) != (ssize_t)BFREE_BLK_SIZE)
		return -EIO;
	vol->dirty = 1;
	return 0;
}

int bfree_blk_sync_super(struct bfree_blk_vol *vol, bfree_blk_super_t *sb)
{
	uint8_t blk[BFREE_BLK_SIZE];

	if (vol == NULL || sb == NULL)
		return -EINVAL;
	sb->magic = BFREE_BLK_MAGIC;
	sb->version = BFREE_BLK_VERSION;
	sb->block_size = BFREE_BLK_SIZE;
	memset(blk, 0, sizeof(blk));
	memcpy(blk, sb, sizeof(*sb));
	return bfree_blk_write(vol, 0, blk);
}

int bfree_blk_load_super(struct bfree_blk_vol *vol, bfree_blk_super_t *sb)
{
	uint8_t blk[BFREE_BLK_SIZE];

	if (vol == NULL || sb == NULL)
		return -EINVAL;
	if (bfree_blk_read(vol, 0, blk) < 0)
		return -EINVAL;
	memcpy(sb, blk, sizeof(*sb));
	if (sb->magic != BFREE_BLK_MAGIC || sb->version != BFREE_BLK_VERSION)
		return -EINVAL;
	if (sb->block_size != BFREE_BLK_SIZE)
		return -EINVAL;
	return 0;
}

uint32_t bfree_blk_alloc(struct bfree_blk_vol *vol, bfree_blk_super_t *sb)
{
	uint32_t blk;

	(void)vol;
	for (blk = BFREE_BLK_DATA_START; blk < sb->block_count; blk++) {
		if (!bitmap_test(sb->bitmap, blk)) {
			bitmap_set(sb->bitmap, blk);
			return blk;
		}
	}
	return 0;
}

void bfree_blk_free(struct bfree_blk_vol *vol, bfree_blk_super_t *sb,
		    uint32_t blk)
{
	uint8_t zero[BFREE_BLK_SIZE];

	(void)vol;
	if (blk < BFREE_BLK_DATA_START || blk >= sb->block_count)
		return;
	bitmap_clear(sb->bitmap, blk);
	memset(zero, 0, sizeof(zero));
	bfree_blk_write(vol, blk, zero);
}

int bfree_blk_write_inode(struct bfree_blk_vol *vol,
			  const bfree_disk_inode_t *ino)
{
	uint8_t blk[BFREE_BLK_SIZE];

	if (vol == NULL || ino == NULL || ino->ino == 0 ||
	    ino->ino > BFREE_BLK_MAX_INODES)
		return -EINVAL;
	memset(blk, 0, sizeof(blk));
	memcpy(blk, ino, sizeof(*ino));
	return bfree_blk_write(vol, inode_table_blk(ino->ino), blk);
}

int bfree_blk_read_inode(struct bfree_blk_vol *vol, uint32_t ino,
			 bfree_disk_inode_t *out)
{
	uint8_t blk[BFREE_BLK_SIZE];

	if (vol == NULL || out == NULL || ino == 0 || ino > BFREE_BLK_MAX_INODES)
		return -EINVAL;
	if (bfree_blk_read(vol, inode_table_blk(ino), blk) < 0)
		return -EINVAL;
	memcpy(out, blk, sizeof(*out));
	return 0;
}
