#include "procfs.h"
#include "fs_ofd.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t cstr_len(const char *s)
{
	size_t n = 0;

	if (s == NULL)
		return 0;
	while (s[n] != '\0')
		n++;
	return n;
}

static int ensure_dir(struct bfree_fs *fs, const char *path)
{
	struct bfree_vnode *vn = bfree_lookup(fs, path);

	if (vn != NULL && vn->type == BFREE_VNODE_DIR)
		return 0;
	return bfree_mkdir(fs, path, 0755);
}

static int set_symlink(struct bfree_fs *fs, const char *path,
		       const char *target)
{
	struct bfree_vnode *vn;
	char *buf;
	size_t n;

	if (fs == NULL || path == NULL || target == NULL)
		return -EINVAL;
	n = cstr_len(target);
	buf = malloc(n + 1U);
	if (buf == NULL)
		return -ENOMEM;
	memcpy(buf, target, n + 1U);

	vn = bfree_lookup(fs, path);
	if (vn == NULL) {
		int rc = bfree_symlink(fs, target, path);

		free(buf);
		return rc;
	}
	if (vn->type != BFREE_VNODE_LNK) {
		free(buf);
		return -EINVAL;
	}
	free(vn->data);
	vn->data = buf;
	vn->size = n;
	return 0;
}

static int set_file_text(struct bfree_fs *fs, const char *path,
			 const char *text)
{
	struct bfree_vnode *vn;
	char *buf;
	size_t n;

	if (fs == NULL || path == NULL || text == NULL)
		return -EINVAL;
	n = cstr_len(text);
	buf = malloc(n + 1U);
	if (buf == NULL)
		return -ENOMEM;
	memcpy(buf, text, n + 1U);

	vn = bfree_lookup(fs, path);
	if (vn == NULL) {
		int rc = bfree_create(fs, path, 0644);

		if (rc != 0) {
			free(buf);
			return rc;
		}
		vn = bfree_lookup(fs, path);
		if (vn == NULL) {
			free(buf);
			return -ENOENT;
		}
	}
	if (vn->type != BFREE_VNODE_FILE) {
		free(buf);
		return -EINVAL;
	}
	free(vn->data);
	vn->data = buf;
	vn->size = n;
	return 0;
}

static void append_hex(char *dst, size_t *pos, size_t cap, unsigned long v)
{
	static const char hex[] = "0123456789abcdef";
	char tmp[16];
	int i;

	for (i = 15; i >= 0; i--) {
		tmp[i] = hex[v & 0xfUL];
		v >>= 4;
	}
	for (i = 0; i < 16 && *pos + 1U < cap; i++)
		dst[(*pos)++] = tmp[i];
}

static void append_str(char *dst, size_t *pos, size_t cap, const char *s)
{
	size_t i;

	for (i = 0; s[i] != '\0' && *pos + 1U < cap; i++)
		dst[(*pos)++] = s[i];
}

int bfree_procfs_init(struct bfree_fs *fs)
{
	if (fs == NULL)
		return -EINVAL;
	if (ensure_dir(fs, "/proc") != 0)
		return -1;
	if (ensure_dir(fs, "/proc/self") != 0)
		return -1;
	if (set_symlink(fs, "/proc/self/exe", "/bin/busybox") != 0)
		return -1;
	if (set_file_text(fs, "/proc/self/maps",
			  "00400000-00540000 r-xp 00000000 00:00 0 /bin/busybox\n") !=
	    0)
		return -1;
	return 0;
}

int bfree_procfs_on_exec(struct bfree_fs *fs, const char *exe_path,
			 uintptr_t heap_base, uintptr_t stack_top)
{
	char maps[320];
	size_t pos = 0;
	const char *path = exe_path != NULL ? exe_path : "/bin/busybox";

	if (fs == NULL)
		return -EINVAL;
	if (set_symlink(fs, "/proc/self/exe", path) != 0)
		return -1;

	append_str(maps, &pos, sizeof(maps),
		   "00400000-00540000 r-xp 00000000 00:00 0 ");
	append_str(maps, &pos, sizeof(maps), path);
	append_str(maps, &pos, sizeof(maps), "\n");
	append_hex(maps, &pos, sizeof(maps), (unsigned long)heap_base);
	append_str(maps, &pos, sizeof(maps), "-");
	append_hex(maps, &pos, sizeof(maps),
		   (unsigned long)(heap_base + 0x1000UL));
	append_str(maps, &pos, sizeof(maps), " rw-p 00000000 00:00 0 [heap]\n");
	append_hex(maps, &pos, sizeof(maps),
		   (unsigned long)(stack_top - 0x2000UL));
	append_str(maps, &pos, sizeof(maps), "-");
	append_hex(maps, &pos, sizeof(maps), (unsigned long)stack_top);
	append_str(maps, &pos, sizeof(maps), " rw-p 00000000 00:00 0 [stack]\n");
	maps[pos < sizeof(maps) ? pos : sizeof(maps) - 1U] = '\0';

	return set_file_text(fs, "/proc/self/maps", maps);
}
