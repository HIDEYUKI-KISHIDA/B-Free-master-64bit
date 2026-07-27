/*
 * ELF64 loader for guest execve (M4).
 */
#ifndef BFREE_ELF_LOAD_H
#define BFREE_ELF_LOAD_H

#include "fs_ofd.h"
#include "vmm.h"

#include <stddef.h>
#include <stdint.h>

#define BFREE_ELF_MAGIC 0x464c457fU

struct bfree_elf_image {
	uint64_t entry;
	size_t   load_size;
};

int bfree_elf_load_fd(struct bfree_fs *fs, int fd, struct bfree_as *as,
		      struct bfree_elf_image *out);
int bfree_elf_load_path(struct bfree_fs *fs, const char *path,
			struct bfree_as *as, struct bfree_elf_image *out);

#endif /* BFREE_ELF_LOAD_H */
