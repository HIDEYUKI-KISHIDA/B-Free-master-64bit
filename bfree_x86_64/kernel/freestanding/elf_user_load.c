/*
 * Load ET_EXEC ELF from initramfs blob into user VAs (M11).
 */
#include "elf_user_load.h"

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *s, int c, unsigned long n);

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;

#define EI_MAG0  0
#define EI_MAG1  1
#define EI_MAG2  2
#define EI_MAG3  3
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

#define BFREE_USER_MAP_LIMIT 0x800000UL
/* First PT_LOAD for our BusyBox images is at 0x400000. */
#define BFREE_USER_LOAD_BASE 0x400000UL

static int in_ring0(void)
{
	uint16_t cs;

	__asm__ volatile("mov %%cs, %0" : "=r"(cs));
	return (cs & 3U) == 0U;
}

int bfree_user_elf_install(const void *blob, size_t len, uintptr_t *entry_out)
{
	return bfree_user_elf_install_ex(blob, len, entry_out, NULL);
}

int bfree_user_elf_install_ex(const void *blob, size_t len, uintptr_t *entry_out,
			      struct bfree_linux_auxinfo *aux_out)
{
	const uint8_t *img = blob;
	const Elf64_Ehdr *eh;
	Elf64_Half i;

	if (blob == NULL || len < sizeof(Elf64_Ehdr) || entry_out == NULL)
		return -1;
	if (!in_ring0())
		return -2;

	eh = (const Elf64_Ehdr *)img;
	if (eh->e_ident[EI_MAG0] != 0x7f || eh->e_ident[EI_MAG1] != 'E' ||
	    eh->e_ident[EI_MAG2] != 'L' || eh->e_ident[EI_MAG3] != 'F')
		return -3;
	if (eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_type != ET_EXEC)
		return -4;
	if (eh->e_phoff == 0 || eh->e_phnum == 0)
		return -5;
	if (eh->e_phoff + (size_t)eh->e_phnum * eh->e_phentsize > len)
		return -6;

	for (i = 0; i < eh->e_phnum; i++) {
		const Elf64_Phdr *ph;
		uint8_t *dest;
		size_t off = (size_t)eh->e_phoff + (size_t)i * eh->e_phentsize;

		if (off + sizeof(Elf64_Phdr) > len)
			return -7;
		ph = (const Elf64_Phdr *)(img + off);
		if (ph->p_type != PT_LOAD)
			continue;
		if (ph->p_vaddr + ph->p_memsz > BFREE_USER_MAP_LIMIT)
			return -8;
		if (ph->p_offset + ph->p_filesz > len)
			return -9;

		dest = (uint8_t *)(uintptr_t)ph->p_vaddr;
		if (ph->p_filesz > 0)
			memcpy(dest, img + ph->p_offset, (unsigned long)ph->p_filesz);
		if (ph->p_memsz > ph->p_filesz)
			memset(dest + ph->p_filesz, 0,
			       (unsigned long)(ph->p_memsz - ph->p_filesz));
	}

	*entry_out = (uintptr_t)eh->e_entry;
	if (aux_out != NULL) {
		/* ET_EXEC: program headers live at load base + e_phoff. */
		aux_out->phdr = BFREE_USER_LOAD_BASE + (uintptr_t)eh->e_phoff;
		aux_out->phent = eh->e_phentsize;
		aux_out->phnum = eh->e_phnum;
		aux_out->entry = (uintptr_t)eh->e_entry;
	}
	return 0;
}
