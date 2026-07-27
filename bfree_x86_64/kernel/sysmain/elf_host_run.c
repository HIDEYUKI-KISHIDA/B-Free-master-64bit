#include "elf_host_run.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static uintptr_t page_floor(uintptr_t addr, long psz)
{
	return addr & ~(uintptr_t)(psz - 1);
}

static uintptr_t page_ceil(uintptr_t addr, long psz)
{
	return (addr + (uintptr_t)psz - 1) & ~(uintptr_t)(psz - 1);
}

int bfree_elf_host_exec(struct bfree_as *as, uintptr_t entry, size_t load_size,
			int *exit_status)
{
	uintptr_t host_entry;
	pid_t pid;
	int st;
	long psz;
	void *base;
	size_t len;

	if (as == NULL || as->mem == NULL || load_size == 0)
		return -EINVAL;
	if (entry >= as->size || load_size > as->size)
		return -EINVAL;

	psz = sysconf(_SC_PAGESIZE);
	if (psz <= 0)
		return -EINVAL;

	host_entry = (uintptr_t)as->mem + entry;
	base = (void *)page_floor((uintptr_t)as->mem, psz);
	len = (size_t)(page_ceil((uintptr_t)as->mem + load_size, psz) -
		       (uintptr_t)base);

	pid = fork();
	if (pid < 0)
		return -errno;
	if (pid == 0) {
		if (mprotect(base, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
			_exit(127);
		((void (*)(void))host_entry)();
		_exit(126);
	}
	if (waitpid(pid, &st, 0) < 0)
		return -errno;
	if (!WIFEXITED(st))
		return -ECHILD;
	if (exit_status != NULL)
		*exit_status = WEXITSTATUS(st);
	return 0;
}
