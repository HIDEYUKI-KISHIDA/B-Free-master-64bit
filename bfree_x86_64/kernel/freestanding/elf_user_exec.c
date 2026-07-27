/*
 * Ring-3 execve: load ET_EXEC, build Linux stack+auxv, iretq resume.
 */
#include "elf_user_exec.h"
#include "elf_user_load.h"
#include "fs_ofd.h"
#include "linux_user_stack.h"
#include "user_boot.h"

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *s, int c, unsigned long n);

#define USER_EXEC_BLOB_ADDR   0x800000UL
#define USER_EXEC_BLOB_MAX    0x200000UL /* 2 MiB — BusyBox ~1.2MiB */
#define USER_EXEC_MAX_ARGC    32

static volatile int user_exec_pending;
static uintptr_t user_exec_entry;
static uintptr_t user_exec_stack;

static int read_vfs_file(struct bfree_fs *fs, const char *path,
			 const void **blob_out, size_t *len_out)
{
	struct bfree_vnode *vn;
	int fd;
	uint8_t *buf;
	ssize_t n;

	if (fs == NULL || path == NULL || blob_out == NULL || len_out == NULL)
		return -1;
	vn = bfree_lookup(fs, path);
	if (vn == NULL || vn->type != BFREE_VNODE_FILE)
		return -1;
	if (vn->size == 0) {
		*blob_out = NULL;
		*len_out = 0;
		return 0;
	}
	if (vn->size > USER_EXEC_BLOB_MAX)
		return -1;
	buf = (uint8_t *)(uintptr_t)USER_EXEC_BLOB_ADDR;
	fd = bfree_open(fs, path, 0, 0);
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

	if (fs == NULL || path == NULL)
		return -2;

	if (read_vfs_file(fs, path, &blob, &blob_len) < 0)
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
