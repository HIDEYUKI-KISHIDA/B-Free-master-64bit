#include "elf_load.h"

#include <errno.h>
#include <string.h>

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int64_t  Elf64_Sxword;
typedef uint64_t Elf64_Xword;

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define ELFCLASS64 2
#define ET_EXEC 2
#define PT_LOAD 1

typedef struct {
	unsigned char e_ident[16];
	Elf64_Half    e_type;
	Elf64_Half    e_machine;
	Elf64_Word    e_version;
	Elf64_Addr    e_entry;
	Elf64_Off     e_phoff;
	Elf64_Off     e_shoff;
	Elf64_Word    e_flags;
	Elf64_Half    e_ehsize;
	Elf64_Half    e_phentsize;
	Elf64_Half    e_phnum;
	Elf64_Half    e_shentsize;
	Elf64_Half    e_shnum;
	Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
	Elf64_Word  p_type;
	Elf64_Word  p_flags;
	Elf64_Off   p_offset;
	Elf64_Addr  p_vaddr;
	Elf64_Addr  p_paddr;
	Elf64_Xword p_filesz;
	Elf64_Xword p_memsz;
	Elf64_Xword p_align;
} Elf64_Phdr;

static int read_at(struct bfree_fs *fs, int fd, off_t off, void *buf,
		   size_t len)
{
	off_t pos;
	ssize_t n;

	pos = bfree_lseek(fs, fd, off, 0);
	if (pos < 0)
		return (int)pos;
	n = bfree_read(fs, fd, buf, len);
	if (n < 0)
		return (int)n;
	if ((size_t)n != len)
		return -EIO;
	return 0;
}

int bfree_elf_load_fd(struct bfree_fs *fs, int fd, struct bfree_as *as,
		      struct bfree_elf_image *out)
{
	Elf64_Ehdr eh;
	Elf64_Half i;
	uint64_t max_end = 0;

	if (fs == NULL || as == NULL || as->mem == NULL || out == NULL)
		return -EINVAL;
	if (read_at(fs, fd, 0, &eh, sizeof(eh)) < 0)
		return -EIO;
	if (eh.e_ident[EI_MAG0] != 0x7f || eh.e_ident[EI_MAG1] != 'E' ||
	    eh.e_ident[EI_MAG2] != 'L' || eh.e_ident[EI_MAG3] != 'F')
		return -ENOEXEC;
	if (eh.e_ident[EI_CLASS] != ELFCLASS64)
		return -ENOEXEC;
	if (eh.e_type != ET_EXEC)
		return -ENOEXEC;
	if (eh.e_phoff == 0 || eh.e_phnum == 0)
		return -ENOEXEC;

	for (i = 0; i < eh.e_phnum; i++) {
		Elf64_Phdr ph;
		off_t ph_off = (off_t)(eh.e_phoff + (Elf64_Off)i * eh.e_phentsize);
		uint64_t seg_end;

		if (read_at(fs, fd, ph_off, &ph, sizeof(ph)) < 0)
			return -EIO;
		if (ph.p_type != PT_LOAD)
			continue;
		if (ph.p_vaddr + ph.p_memsz > as->size)
			return -ENOMEM;
		if (ph.p_filesz > 0) {
			if (read_at(fs, fd, (off_t)ph.p_offset,
				    as->mem + ph.p_vaddr, (size_t)ph.p_filesz) < 0)
				return -EIO;
		}
		if (ph.p_memsz > ph.p_filesz)
			memset(as->mem + ph.p_vaddr + ph.p_filesz, 0,
			       (size_t)(ph.p_memsz - ph.p_filesz));
		seg_end = ph.p_vaddr + ph.p_memsz;
		if (seg_end > max_end)
			max_end = seg_end;
	}

	out->entry = eh.e_entry;
	out->load_size = (size_t)max_end;
	return 0;
}

int bfree_elf_load_path(struct bfree_fs *fs, const char *path,
			struct bfree_as *as, struct bfree_elf_image *out)
{
	int fd;
	int rc;

	fd = bfree_open(fs, path, 0, 0);
	if (fd < 0)
		return fd;
	rc = bfree_elf_load_fd(fs, fd, as, out);
	bfree_close(fs, fd);
	return rc;
}
