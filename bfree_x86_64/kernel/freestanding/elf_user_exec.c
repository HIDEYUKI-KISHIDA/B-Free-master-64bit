/*
 * Ring-3 execve: load ET_EXEC, build argv trampoline, iretq resume (M15).
 */
#include "elf_user_exec.h"
#include "elf_user_load.h"
#include "fs_ofd.h"
#include "user_boot.h"

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *s, int c, unsigned long n);

#define USER_EXEC_TRAMP_ADDR  0x100000UL
#define USER_EXEC_ARGV_ADDR   0x101000UL
#define USER_EXEC_STR_ADDR    0x102000UL
#define USER_EXEC_MAX_ARGC    16
#define USER_EXEC_STR_BYTES   2048UL

static volatile int user_exec_pending;
static uintptr_t user_exec_entry;
static uintptr_t user_exec_stack;

extern char guest_exec_tramp[];
extern char guest_exec_tramp_end[];
extern uint64_t guest_exec_argc_slot;
extern uint64_t guest_exec_argv_slot;
extern uint64_t guest_exec_envp_slot;
extern uint64_t guest_exec_entry_slot;

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
	buf = (uint8_t *)(uintptr_t)USER_EXEC_STR_ADDR;
	if (vn->size > USER_EXEC_STR_BYTES / 2U)
		return -1;
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

static uintptr_t slot_offset(const void *slot)
{
	return (uintptr_t)slot - (uintptr_t)guest_exec_tramp;
}

static int build_tramp(uintptr_t entry, int argc, char *const argv[])
{
	uintptr_t tramp_len;
	uint64_t *argv_vec;
	uint64_t *envp_vec;
	uint64_t *entry_patch;
	char *str;
	size_t str_used;
	int i;

	if (argc < 0 || argc > USER_EXEC_MAX_ARGC)
		return -1;

	tramp_len = (uintptr_t)guest_exec_tramp_end -
		    (uintptr_t)guest_exec_tramp;
	memcpy((void *)USER_EXEC_TRAMP_ADDR, guest_exec_tramp, tramp_len);

	argv_vec = (uint64_t *)USER_EXEC_ARGV_ADDR;
	envp_vec = argv_vec + (size_t)argc + 1U;
	str = (char *)(uintptr_t)USER_EXEC_STR_ADDR;
	str_used = 0;

	for (i = 0; i < argc; i++) {
		size_t len;

		if (argv[i] == NULL)
			return -1;
		len = 0;
		while (argv[i][len] != '\0')
			len++;
		if (str_used + len + 1U > USER_EXEC_STR_BYTES)
			return -1;
		memcpy(str + str_used, argv[i], len + 1U);
		argv_vec[i] = (uint64_t)(uintptr_t)(str + str_used);
		str_used += len + 1U;
	}
	argv_vec[argc] = 0;
	envp_vec[0] = 0;

	*(uint64_t *)(USER_EXEC_TRAMP_ADDR +
		      slot_offset(&guest_exec_argc_slot)) = (uint64_t)argc;
	*(uint64_t *)(USER_EXEC_TRAMP_ADDR +
		      slot_offset(&guest_exec_argv_slot)) =
		(uint64_t)(uintptr_t)argv_vec;
	*(uint64_t *)(USER_EXEC_TRAMP_ADDR +
		      slot_offset(&guest_exec_envp_slot)) =
		(uint64_t)(uintptr_t)envp_vec;
	entry_patch = (uint64_t *)(USER_EXEC_TRAMP_ADDR +
				   slot_offset(&guest_exec_entry_slot));
	*entry_patch = (uint64_t)entry;

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
	int argc;

	(void)envp;
	if (fs == NULL || path == NULL)
		return -2;

	if (read_vfs_file(fs, path, &blob, &blob_len) < 0)
		return -1;
	if (blob_len == 0)
		return -1;

	/* Load ELF above trampoline/string scratch. */
	if (bfree_user_elf_install(blob, blob_len, &entry) != 0)
		return -1;

	for (argc = 0; argv && argv[argc]; argc++)
		;
	if (argc == 0)
		return -1;
	if (build_tramp(entry, argc, argv) != 0)
		return -1;

	user_exec_pending = 1;
	user_exec_entry = USER_EXEC_TRAMP_ADDR;
	user_exec_stack = BFREE_USER_STACK_TOP;
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
