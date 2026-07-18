// elf_loader.h - 簡易ELFローダ雛形
#pragma once
#include <stdint.h>

typedef struct {
    uint64_t entry;
    uint64_t phdr_vaddr;
    uint16_t phnum;
    uint16_t phentsize;
    int      valid;
} bfree_loaded_elf_info_t;

int  load_elf_image(const char *filename, void **entry, void *page_table_base);
void bfree_loaded_elf_info_get(bfree_loaded_elf_info_t *out);
/* Read bytes from Multiboot initrd module by basename (guest legacy mount). */
int  bfree_initrd_read(const char *filename, uint64_t offset, void *buf, uint64_t size);
void initrd_register(const char *name, uint8_t *data, uint64_t size);
/* Copy kernel PT (incl. initrd VA window) into task page tables after module scan. */
void initrd_sync_user_page_tables(void);
/* Zero PMM pages from kernel CR3 (identity <128 MiB, else loader staging). */
void bfree_kernel_zero_phys_page(uint64_t phys);
void bfree_kernel_phys_io_begin(void);
void bfree_kernel_phys_io_end(void);
int bfree_kernel_clear_phys(uint64_t phys, uint64_t len);
int bfree_kernel_poke_phys(uint64_t phys, const void *src, uint64_t len);
int bfree_kernel_peek_phys(uint64_t phys, void *dst, uint64_t len);
