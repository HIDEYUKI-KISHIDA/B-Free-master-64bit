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

static void append_dec(char *dst, size_t *pos, size_t cap,
			 unsigned long v)
{
	char tmp[32];
	size_t n = 0;

	if (v == 0) {
		if (*pos + 1U < cap)
			dst[(*pos)++] = '0';
		return;
	}
	while (v > 0 && n < sizeof(tmp)) {
		tmp[n++] = (char)('0' + (v % 10UL));
		v /= 10UL;
	}
	while (n > 0 && *pos + 1U < cap)
		dst[(*pos)++] = tmp[--n];
}

static void vfs_basename(char *dst, size_t dst_len, const char *path)
{
	const char *p;
	const char *last = path;
	size_t i = 0;

	if (dst == NULL || dst_len == 0) {
		return;
	}
	if (path == NULL) {
		dst[0] = '\0';
		return;
	}

	for (p = path; *p != '\0'; p++) {
		if (*p == '/')
			last = p + 1;
	}
	for (; *last != '\0' && i + 1U < dst_len; i++) {
		dst[i] = *last;
		last++;
	}
	dst[i] = '\0';
}

static int set_proc_self_status(struct bfree_fs *fs, const char *name)
{
	char status[512];
	size_t pos = 0;
	unsigned long one = 1UL;

	if (fs == NULL || name == NULL)
		return -EINVAL;

	append_str(status, &pos, sizeof(status), "Name:\t");
	append_str(status, &pos, sizeof(status), name);
	append_str(status, &pos, sizeof(status),
		   "\nState:\tR (running)\nTgid:\t");
	append_dec(status, &pos, sizeof(status), one);
	append_str(status, &pos, sizeof(status), "\nPid:\t");
	append_dec(status, &pos, sizeof(status), one);
	append_str(status, &pos, sizeof(status),
		   "\nPPid:\t0\nTracerPid:\t0\nUid:\t0\t0\t0\t0\nGid:\t0\t0\t0\t0\nFDSize:\t64\nGroups:\t0\nNStgid:\t1\nThreads:\t");
	append_dec(status, &pos, sizeof(status), one);
	append_str(status, &pos, sizeof(status),
		   "\nVmPeak:\t0 kB\nVmSize:\t0 kB\nVmLck:\t0 kB\nVmPin:\t0 kB\nVmHWM:\t0 kB\nVmRSS:\t0 kB\nVmData:\t0 kB\nVmStk:\t0 kB\nVmExe:\t0 kB\nVmLib:\t0 kB\nVmPTE:\t0 kB\nVmSwap:\t0 kB\nHugetlbPages:\t0\nvoluntary_ctxt_switches:\t0\nnonvoluntary_ctxt_switches:\t0\n");

	status[pos < sizeof(status) ? pos : sizeof(status) - 1U] = '\0';
	return set_file_text(fs, "/proc/self/status", status);
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
	if (set_file_text(fs, "/proc/meminfo",
			  "MemTotal:\t16384 kB\n"
			  "MemFree:\t8192 kB\n"
			  "Buffers:\t0 kB\n"
			  "Cached:\t0 kB\n"
			  "SwapCached:\t0 kB\n"
			  "SwapTotal:\t0 kB\n"
			  "SwapFree:\t0 kB\n") != 0)
		return -1;
	if (set_file_text(fs, "/proc/cpuinfo",
			  "processor\t: 0\n"
			  "vendor_id\t: GenuineIntel\n"
			  "cpu family\t: 6\n"
			  "model\t\t: 79\n"
			  "model name\t: B-Free x86_64\n"
			  "cpu MHz\t\t: 1000.000\n"
			  "flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic\n") != 0)
		return -1;
	if (set_file_text(fs, "/proc/self/maps",
			  "00400000-00540000 r-xp 00000000 00:00 0 /bin/busybox\n") !=
	    0)
		return -1;
	if (set_proc_self_status(fs, "busybox") != 0)
		return -1;
	return 0;
}

int bfree_procfs_on_exec(struct bfree_fs *fs, const char *exe_path,
			 uintptr_t heap_base, uintptr_t stack_top)
{
	char maps[320];
	char name[64];
	size_t pos = 0;
	const char *path = exe_path != NULL ? exe_path : "/bin/busybox";

	if (fs == NULL)
		return -EINVAL;
	vfs_basename(name, sizeof(name), path);
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

	if (set_proc_self_status(fs, name) != 0)
		return -1;
	if (set_file_text(fs, "/proc/self/maps", maps) != 0)
		return -1;
	return 0;
}
