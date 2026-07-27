/*
 * Ring-3 execve: load ET_EXEC, build Linux stack+auxv, iretq resume.
 */
#include "elf_user_exec.h"
#include "elf_user_load.h"
#include "fs_ofd.h"
#include "linux_user_stack.h"
#include "procfs.h"
#include "user_boot.h"
#include "vmm.h"

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *s, int c, unsigned long n);

#define USER_EXEC_BLOB_ADDR   0x03000000UL
#define USER_EXEC_BLOB_MAX    BFREE_USER_EXEC_BLOB_MAX
#define USER_EXEC_MAX_ARGC    32
#define USER_EXEC_MAX_SYMLINK 8

static volatile int user_exec_pending;
static uintptr_t user_exec_entry;
static uintptr_t user_exec_stack;
static char user_exec_path[BFREE_MAX_PATH];

static size_t cstr_path_len(const char *s)
{
	size_t n = 0;

	while (s[n] != '\0')
		n++;
	return n;
}

static void copy_path(char *dst, const char *src)
{
	size_t i;

	for (i = 0; src[i] != '\0' && i + 1U < BFREE_MAX_PATH; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

size_t bfree_user_exec_blob_max(void)
{
	return USER_EXEC_BLOB_MAX;
}

/*
 * Resolve path through symlinks to a regular file vnode.
 * Writes the final absolute-ish path into out_path when non-NULL.
 */
int bfree_user_exec_resolve(struct bfree_fs *fs, const char *path,
			    char *out_path, size_t out_len,
			    struct bfree_vnode **file_out)
{
	struct bfree_vnode *vn;
	char cur[BFREE_MAX_PATH];
	int depth;

	if (fs == NULL || path == NULL || file_out == NULL)
		return -1;
	if (path[0] == '\0' || cstr_path_len(path) >= BFREE_MAX_PATH)
		return -1;
	copy_path(cur, path);

	for (depth = 0; depth < USER_EXEC_MAX_SYMLINK; depth++) {
		vn = bfree_lookup(fs, cur);
		if (vn == NULL)
			return -1;
		if (vn->type == BFREE_VNODE_FILE) {
			if (out_path != NULL && out_len > 0) {
				size_t n = cstr_path_len(cur);

				if (n + 1U > out_len)
					return -1;
				memcpy(out_path, cur, n + 1U);
			}
			*file_out = vn;
			return 0;
		}
		if (vn->type != BFREE_VNODE_LNK || vn->data == NULL)
			return -1;
		if (cstr_path_len(vn->data) >= BFREE_MAX_PATH)
			return -1;
		copy_path(cur, vn->data);
	}
	return -1;
}

static int read_vfs_file(struct bfree_fs *fs, const char *path,
			 const void **blob_out, size_t *len_out,
			 char *resolved, size_t resolved_len)
{
	struct bfree_vnode *vn;
	int fd;
	uint8_t *buf;
	ssize_t n;

	if (fs == NULL || path == NULL || blob_out == NULL || len_out == NULL)
		return -1;
	if (bfree_user_exec_resolve(fs, path, resolved, resolved_len, &vn) != 0)
		return -1;
	if (vn->size == 0) {
		*blob_out = NULL;
		*len_out = 0;
		return 0;
	}
	if (vn->size > USER_EXEC_BLOB_MAX)
		return -1;
	buf = (uint8_t *)(uintptr_t)USER_EXEC_BLOB_ADDR;
	fd = bfree_open(fs, resolved != NULL ? resolved : path, 0, 0);
	if (fd < 0)
		return -1;
	n = bfree_read(fs, fd, buf, vn->size);
	bfree_close(fs, fd);
	if (n < 0 || (size_t)n != vn->size)
		return -1;
	*blob_out = buf;
	*len_out = vn->size;
	return 0;
}

int bfree_user_exec_pending(void)
{
	return user_exec_pending;
}

void bfree_user_exec_clear(void)
{
	user_exec_pending = 0;
	user_exec_entry = 0;
	user_exec_stack = 0;
}

int bfree_user_execve_ring3(struct bfree_fs *fs, const char *path,
			    char *const argv[], char *const envp[])
{
	const void *blob;
	size_t blob_len;
	uintptr_t entry;
	uintptr_t rsp;
	struct bfree_linux_auxinfo aux;
	int argc;
	char resolved[BFREE_MAX_PATH];

	if (fs == NULL || path == NULL)
		return -2;

	if (read_vfs_file(fs, path, &blob, &blob_len, resolved,
			  sizeof(resolved)) < 0)
		return -1;
	if (blob_len == 0)
		return -1;

	if (bfree_user_elf_install_ex(blob, blob_len, &entry, &aux) != 0)
		return -1;

	for (argc = 0; argv && argv[argc]; argc++)
		;
	if (argc == 0 || argc > USER_EXEC_MAX_ARGC)
		return -1;

	rsp = bfree_linux_user_stack_build(BFREE_USER_STACK_TOP, argc, argv,
					   envp, &aux);
	if (rsp == 0)
		return -1;

	memcpy(user_exec_path, resolved, sizeof(user_exec_path));
	(void)bfree_procfs_on_exec(fs, user_exec_path, BFREE_USER_HEAP_BASE,
				   BFREE_USER_STACK_TOP);

	user_exec_pending = 1;
	user_exec_entry = entry;
	user_exec_stack = rsp;
	return 0;
}

void bfree_syscall_exec_resume_if_needed(void)
{
	uintptr_t entry;
	uintptr_t stack;

	if (!user_exec_pending)
		return;
	entry = user_exec_entry;
	stack = user_exec_stack;
	bfree_user_exec_clear();
	bfree_user_boot_exec(entry, stack);
}
