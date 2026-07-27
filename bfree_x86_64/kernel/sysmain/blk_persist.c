/*
 * Persist vnode tree and file payloads to block volume.
 */
#include "blk_persist.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct bfree_vnode *vnode_by_ino(struct bfree_fs *fs, uint32_t ino)
{
	size_t i;

	if (ino == 0)
		return NULL;
	if (ino == fs->super.root_ino)
		return &fs->root;
	for (i = 0; i < fs->vnode_count; i++) {
		if (fs->vnodes[i] && fs->vnodes[i]->ino == ino)
			return fs->vnodes[i];
	}
	return NULL;
}

static int vnode_register(struct bfree_fs *fs, struct bfree_vnode *vn)
{
	if (fs->vnode_count >= BFREE_BLK_MAX_INODES)
		return -ENOSPC;
	fs->vnodes[fs->vnode_count++] = vn;
	return 0;
}

static void vnode_destroy_subtree(struct bfree_vnode *vn)
{
	size_t i;

	if (vn == NULL)
		return;
	for (i = 0; i < vn->child_count; i++)
		vnode_destroy_subtree(vn->children[i]);
	free(vn->data);
	free(vn);
}

static void vnode_apply_dino(struct bfree_vnode *vn,
			     const bfree_disk_inode_t *dino)
{
	size_t i;

	snprintf(vn->name, sizeof(vn->name), "%s", dino->name);
	vn->ino = dino->ino;
	vn->type = (dino->type == BFREE_DINO_DIR) ? BFREE_VNODE_DIR :
						    BFREE_VNODE_FILE;
	vn->size = dino->size;
	vn->unlinked = (dino->flags & BFREE_DINO_FLAG_UNLINKED) ? 1 : 0;
	vn->data_blk_count = dino->data_blk_count;
	for (i = 0; i < dino->data_blk_count; i++)
		vn->data_blks[i] = dino->data_blks[i];
}

static struct bfree_vnode *vnode_alloc_from_disk(struct bfree_fs *fs,
						 const bfree_disk_inode_t *dino)
{
	struct bfree_vnode *vn;

	vn = calloc(1, sizeof(*vn));
	if (vn == NULL)
		return NULL;
	vnode_apply_dino(vn, dino);
	if (vnode_register(fs, vn) < 0) {
		free(vn);
		return NULL;
	}
	return vn;
}

static int link_children(struct bfree_fs *fs, struct bfree_vnode *parent,
			 const bfree_disk_inode_t *dino)
{
	size_t i;

	parent->child_count = 0;
	for (i = 0; i < dino->child_count; i++) {
		struct bfree_vnode *child;

		child = vnode_by_ino(fs, dino->child_inos[i]);
		if (child == NULL)
			return -EIO;
		if (parent->child_count >= BFREE_MAX_CHILD)
			return -ENOSPC;
		parent->children[parent->child_count++] = child;
		child->parent = parent;
	}
	return 0;
}

static int load_tree(struct bfree_fs *fs)
{
	uint32_t ino;
	bfree_disk_inode_t root_dino;

	if (bfree_blk_read_inode(fs->vol, fs->super.root_ino, &root_dino) < 0)
		return -EIO;
	vnode_apply_dino(&fs->root, &root_dino);
	fs->root.parent = NULL;

	for (ino = 2; ino <= fs->super.inode_count; ino++) {
		bfree_disk_inode_t dino;

		if (bfree_blk_read_inode(fs->vol, ino, &dino) < 0)
			return -EIO;
		if (dino.ino == 0)
			continue;
		if (vnode_alloc_from_disk(fs, &dino) == NULL)
			return -ENOMEM;
	}

	for (ino = 1; ino <= fs->super.inode_count; ino++) {
		bfree_disk_inode_t dino;
		struct bfree_vnode *vn;

		if (bfree_blk_read_inode(fs->vol, ino, &dino) < 0)
			return -EIO;
		vn = vnode_by_ino(fs, dino.ino);
		if (vn == NULL)
			continue;
		if (vn->type == BFREE_VNODE_DIR && link_children(fs, vn, &dino) < 0)
			return -EIO;
		if (vn != &fs->root)
			vn->parent = vnode_by_ino(fs, dino.parent_ino);
	}

	return 0;
}

static void disk_inode_from_vnode(const struct bfree_vnode *vn,
				  bfree_disk_inode_t *dino)
{
	size_t i;

	memset(dino, 0, sizeof(*dino));
	dino->ino = vn->ino;
	dino->type = (vn->type == BFREE_VNODE_DIR) ? BFREE_DINO_DIR :
						     BFREE_DINO_FILE;
	dino->name_len = (uint16_t)strlen(vn->name);
	snprintf(dino->name, sizeof(dino->name), "%s", vn->name);
	dino->parent_ino = vn->parent ? vn->parent->ino : 0;
	dino->size = (uint32_t)vn->size;
	dino->data_blk_count = vn->data_blk_count;
	for (i = 0; i < vn->data_blk_count; i++)
		dino->data_blks[i] = vn->data_blks[i];
	if (vn->unlinked)
		dino->flags |= BFREE_DINO_FLAG_UNLINKED;
	if (vn->type == BFREE_VNODE_DIR) {
		dino->child_count = (uint32_t)vn->child_count;
		for (i = 0; i < vn->child_count; i++)
			dino->child_inos[i] = vn->children[i]->ino;
	}
}

static int assign_inos(struct bfree_vnode *vn, bfree_blk_super_t *sb)
{
	size_t i;

	if (vn->ino == 0) {
		if (sb->next_ino > BFREE_BLK_MAX_INODES)
			return -ENOSPC;
		vn->ino = sb->next_ino++;
		if (vn->ino > sb->inode_count)
			sb->inode_count = vn->ino;
	}
	for (i = 0; i < vn->child_count; i++) {
		if (assign_inos(vn->children[i], sb) < 0)
			return -ENOSPC;
	}
	return 0;
}

static int sync_vnode(struct bfree_fs *fs, struct bfree_vnode *vn)
{
	bfree_disk_inode_t dino;
	size_t i;

	if (vn->ino == 0)
		return -EINVAL;

	if (vn->type == BFREE_VNODE_FILE && vn->data != NULL && vn->size > 0) {
		size_t off = 0;

		while (off < vn->size) {
			uint8_t blkbuf[BFREE_BLK_SIZE];
			uint32_t blk;
			size_t chunk = vn->size - off;

			if (chunk > BFREE_BLK_SIZE)
				chunk = BFREE_BLK_SIZE;
			if (vn->data_blk_count >= BFREE_BLK_MAX_FILE_BLKS)
				return -ENOSPC;
			blk = bfree_blk_alloc(fs->vol, &fs->super);
			if (blk == 0)
				return -ENOSPC;
			memset(blkbuf, 0, sizeof(blkbuf));
			memcpy(blkbuf, vn->data + off, chunk);
			if (bfree_blk_write(fs->vol, blk, blkbuf) < 0)
				return -EIO;
			vn->data_blks[vn->data_blk_count++] = blk;
			off += chunk;
		}
		free(vn->data);
		vn->data = NULL;
	}

	disk_inode_from_vnode(vn, &dino);
	if (bfree_blk_write_inode(fs->vol, &dino) < 0)
		return -EIO;

	for (i = 0; i < vn->child_count; i++) {
		if (sync_vnode(fs, vn->children[i]) < 0)
			return -EIO;
	}
	return 0;
}

static void clear_ephemeral_tree(struct bfree_fs *fs)
{
	size_t i;

	while (fs->root.child_count > 0) {
		struct bfree_vnode *child = fs->root.children[0];

		fs->root.children[0] =
			fs->root.children[--fs->root.child_count];
		vnode_destroy_subtree(child);
	}
	for (i = 0; i < fs->vnode_count; i++)
		vnode_destroy_subtree(fs->vnodes[i]);
	fs->vnode_count = 0;
	memset(&fs->root, 0, sizeof(fs->root));
}

int bfree_fs_mount(struct bfree_fs *fs, const char *path)
{
	if (fs == NULL || path == NULL)
		return -EINVAL;
	if (fs->mounted)
		return -EBUSY;

	fs->vol = calloc(1, sizeof(*fs->vol));
	if (fs->vol == NULL)
		return -ENOMEM;

	if (bfree_blk_open(fs->vol, path) < 0) {
		free(fs->vol);
		fs->vol = NULL;
		return -EINVAL;
	}
	if (bfree_blk_load_super(fs->vol, &fs->super) < 0) {
		bfree_blk_close(fs->vol);
		free(fs->vol);
		fs->vol = NULL;
		return -EINVAL;
	}

	clear_ephemeral_tree(fs);
	if (load_tree(fs) < 0) {
		bfree_fs_umount(fs);
		return -EIO;
	}

	fs->mounted = 1;
	return 0;
}

int bfree_fs_sync(struct bfree_fs *fs)
{
	if (fs == NULL || !fs->mounted || fs->vol == NULL)
		return -EINVAL;

	if (fs->root.ino == 0)
		fs->root.ino = fs->super.root_ino;
	if (assign_inos(&fs->root, &fs->super) < 0)
		return -ENOSPC;
	if (sync_vnode(fs, &fs->root) < 0)
		return -EIO;
	if (bfree_blk_sync_super(fs->vol, &fs->super) < 0)
		return -EIO;
	return 0;
}

void bfree_fs_umount(struct bfree_fs *fs)
{
	size_t i;

	if (fs == NULL)
		return;
	if (fs->vol != NULL) {
		bfree_blk_close(fs->vol);
		free(fs->vol);
		fs->vol = NULL;
	}
	for (i = 0; i < fs->vnode_count; i++)
		vnode_destroy_subtree(fs->vnodes[i]);
	memset(fs->vnodes, 0, sizeof(fs->vnodes));
	fs->vnode_count = 0;
	fs->mounted = 0;
	memset(&fs->super, 0, sizeof(fs->super));
	memset(&fs->root, 0, sizeof(fs->root));
}

ssize_t bfree_blk_file_read(struct bfree_fs *fs, struct bfree_vnode *vn,
			    off_t offset, void *buf, size_t count)
{
	size_t done = 0;
	uint8_t blkbuf[BFREE_BLK_SIZE];

	if (fs == NULL || vn == NULL || buf == NULL || offset < 0)
		return -EINVAL;
	if ((size_t)offset >= vn->size)
		return 0;
	if (count > vn->size - (size_t)offset)
		count = vn->size - (size_t)offset;

	while (done < count) {
		size_t blk_idx = (size_t)offset / BFREE_BLK_SIZE;
		size_t blk_off = (size_t)offset % BFREE_BLK_SIZE;
		size_t chunk = BFREE_BLK_SIZE - blk_off;
		uint32_t blk;

		if (blk_idx >= vn->data_blk_count)
			break;
		if (chunk > count - done)
			chunk = count - done;
		blk = vn->data_blks[blk_idx];
		if (bfree_blk_read(fs->vol, blk, blkbuf) < 0)
			return -EIO;
		memcpy((uint8_t *)buf + done, blkbuf + blk_off, chunk);
		done += chunk;
		offset += (off_t)chunk;
	}
	return (ssize_t)done;
}

ssize_t bfree_blk_file_write(struct bfree_fs *fs, struct bfree_vnode *vn,
			     off_t offset, const void *buf, size_t count)
{
	size_t done = 0;
	uint8_t blkbuf[BFREE_BLK_SIZE];

	if (fs == NULL || vn == NULL || buf == NULL || offset < 0)
		return -EINVAL;

	while (done < count) {
		size_t blk_idx = (size_t)offset / BFREE_BLK_SIZE;
		size_t blk_off = (size_t)offset % BFREE_BLK_SIZE;
		size_t chunk = BFREE_BLK_SIZE - blk_off;
		uint32_t blk;

		if (chunk > count - done)
			chunk = count - done;

		while (blk_idx >= vn->data_blk_count) {
			if (vn->data_blk_count >= BFREE_BLK_MAX_FILE_BLKS)
				return -ENOSPC;
			blk = bfree_blk_alloc(fs->vol, &fs->super);
			if (blk == 0)
				return -ENOSPC;
			memset(blkbuf, 0, sizeof(blkbuf));
			if (bfree_blk_write(fs->vol, blk, blkbuf) < 0)
				return -EIO;
			vn->data_blks[vn->data_blk_count++] = blk;
		}

		blk = vn->data_blks[blk_idx];
		if (bfree_blk_read(fs->vol, blk, blkbuf) < 0)
			return -EIO;
		memcpy(blkbuf + blk_off, (const uint8_t *)buf + done, chunk);
		if (bfree_blk_write(fs->vol, blk, blkbuf) < 0)
			return -EIO;

		done += chunk;
		offset += (off_t)chunk;
		if ((size_t)offset > vn->size)
			vn->size = (size_t)offset;
	}
	return (ssize_t)done;
}

void bfree_blk_file_free(struct bfree_fs *fs, struct bfree_vnode *vn)
{
	size_t i;

	if (fs == NULL || vn == NULL || !fs->mounted)
		return;
	for (i = 0; i < vn->data_blk_count; i++)
		bfree_blk_free(fs->vol, &fs->super, vn->data_blks[i]);
	vn->data_blk_count = 0;
}
