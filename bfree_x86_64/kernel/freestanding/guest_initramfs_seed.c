/*
 * Seed synthetic VFS from embedded initramfs (M15).
 */
#include "guest_initramfs_seed.h"
#include "fs_ofd.h"
#include "initramfs.h"

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, unsigned long n);
int snprintf(char *str, unsigned long size, const char *format, ...);

static char *strrchr_local(const char *s, int c)
{
	const char *last = NULL;

	while (*s) {
		if ((unsigned char)*s == (unsigned char)c)
			last = s;
		s++;
	}
	return (char *)last;
}

static int mkdir_p(struct bfree_fs *fs, const char *path)
{
	char parent[256];
	char *slash;

	if (path == NULL || path[0] != '/')
		return -1;
	if (bfree_lookup(fs, path) != NULL)
		return 0;
	slash = strrchr_local(path, '/');
	if (slash != NULL && slash != path) {
		size_t plen = (size_t)(slash - path);

		if (plen >= sizeof(parent))
			return -1;
		memcpy(parent, path, plen);
		parent[plen] = '\0';
		if (mkdir_p(fs, parent) < 0)
			return -1;
	}
	return bfree_mkdir(fs, path, 0755);
}

static int write_file(struct bfree_fs *fs, const char *path,
		      const void *data, size_t size)
{
	char parent[256];
	char *slash;
	int fd;
	ssize_t n;

	if (path == NULL || fs == NULL)
		return -1;
	slash = strrchr_local(path, '/');
	if (slash != NULL && slash != path) {
		size_t plen = (size_t)(slash - path);

		if (plen >= sizeof(parent))
			return -1;
		memcpy(parent, path, plen);
		parent[plen] = '\0';
		if (mkdir_p(fs, parent) < 0)
			return -1;
	}
	if (bfree_lookup(fs, path) != NULL)
		(void)bfree_unlink(fs, path);
	if (bfree_create(fs, path, 0644) < 0)
		return -1;
	fd = bfree_open(fs, path, 0, 0);
	if (fd < 0)
		return -1;
	if (size > 0) {
		n = bfree_write(fs, fd, data, size);
		if (n < 0 || (size_t)n != size) {
			bfree_close(fs, fd);
			return -1;
		}
	}
	return bfree_close(fs, fd);
}

static int name_has_prefix(const char *name, const char *prefix)
{
	size_t i = 0;

	while (prefix[i] != '\0') {
		if (name[i] != prefix[i])
			return 0;
		i++;
	}
	return 1;
}

static void seed_cb(const char *name, const void *data, size_t size, void *arg)
{
	struct bfree_fs *fs = arg;
	char path[256];

	if (name == NULL || fs == NULL)
		return;
	if (!name_has_prefix(name, "ash_regress/") &&
	    !name_has_prefix(name, "bin/"))
		return;
	snprintf(path, sizeof(path), "/%s", name);
	(void)write_file(fs, path, data, size);
}

static void seed_busybox_applets(struct bfree_fs *fs)
{
	static const char *const applets[] = {
		"cat", "echo", "sh", "ash", "true", "false", "sleep",
		"date", "id", "ln", "readlink", "stat",
		"ls", "cp", "mv", "mkdir", "rm", "rmdir", NULL
	};
	const void *blob;
	size_t len;
	char path[128];
	int i;

	if (bfree_initramfs_lookup("bin/busybox", &blob, &len) != 0)
		return;
	for (i = 0; applets[i] != NULL; i++) {
		snprintf(path, sizeof(path), "/bin/%s", applets[i]);
		(void)write_file(fs, path, blob, len);
	}
}

void bfree_guest_initramfs_seed_vfs(struct bfree_fs *fs)
{
	if (fs == NULL)
		return;
	(void)bfree_initramfs_foreach(seed_cb, fs);
	seed_busybox_applets(fs);
}
