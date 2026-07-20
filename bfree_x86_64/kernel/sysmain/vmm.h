#ifndef VMM_H

#define VMM_H



#include <stdint.h>

#ifdef __cplusplus

extern "C" {

#endif



#define PAGE_SIZE 4096ULL

#define PML4E_COUNT 512

#define PDPTE_COUNT 512

#define PDE_COUNT 512

#define PTE_COUNT 512

/* Embedded PT slots cover 160*2MiB = 320 MiB without bloating page_table_t BSS. */

#define PT_EMBEDDED_COUNT 160

/* Guest heap (BFREE_GUEST_HEAP_LIMIT 0x2A000000) needs pd slot 336*2MiB = 672 MiB. */

#define PT_LEVEL_MAX 336

#define PT_EXT_COUNT (PT_LEVEL_MAX - PT_EMBEDDED_COUNT)

#define PT_LEVEL_COUNT PT_LEVEL_MAX

#define VMM_USER_VA_BYTES ((uint64_t)PT_LEVEL_MAX * 0x200000ULL)

typedef uint64_t pte_t;

typedef struct page_table {

    pte_t pml4[PML4E_COUNT];

    pte_t pdpt[PDPTE_COUNT];

    pte_t pd[PDE_COUNT];

    pte_t pt[PT_EMBEDDED_COUNT][PTE_COUNT];

    pte_t *pt_ext[PT_EXT_COUNT]; /* lazily allocated; pt_ext[0] -> pd slot 160 */

} page_table_t;



void vmm_init_kernel_page_table(void);

void vmm_activate_kernel_page_table(void);

void vmm_clone_kernel_page_table(page_table_t *dst);

void vmm_dump_kernel_page_table(void);

int vmm_map_page(page_table_t *pt, uint64_t vaddr, uint64_t paddr, uint64_t flags);

/* 2 MiB huge-page map for MMIO (e.g. virtio-vga VRAM at 0xFD000000); uses shared pdpt[3]. */

int vmm_map_mmio_huge(page_table_t *pt, uint64_t vaddr, uint64_t paddr);

int vmm_unmap_page(page_table_t *pt, uint64_t vaddr);

/*
 * After mapping a PMM frame as a user page at some other VA, drop the
 * supervisor identity PTE at VA==phys (clone artifact). Leaving that alias
 * lets ring0 under the task CR3 wipe live ELF/stack frames via VA=phys
 * (observed: busybox .text at PA 0x8BF000 zeroed → fopen #PF on .rodata).
 */
void vmm_drop_identity_alias(page_table_t *pt, uint64_t phys);

/* True when vaddr has a real user RW mapping (not supervisor identity clone). */

int vmm_user_page_mapped(page_table_t *pt, uint64_t vaddr);

/* Resolve a user mapping to its physical address (-1 on miss/identity). */
int vmm_user_virt_to_phys(page_table_t *pt, uint64_t vaddr, uint64_t *paddr_out);

/* True if any User PTE under pt targets this physical frame. */
int vmm_user_maps_phys(page_table_t *pt, uint64_t paddr);

/* Unmap and pmm_free every non-identity user page under pt (child AS teardown).
 * If keep is non-NULL, never free frames still mapped as User in keep (parent). */
void vmm_destroy_user_mappings(page_table_t *pt);
void vmm_destroy_user_mappings_keep(page_table_t *pt, page_table_t *keep);

/* Copy every non-identity user page from src into dst (dst must already be
 * kernel-cloned). Used so cooperative vfork children can mutate a private
 * copy while the frozen parent AS stays intact. Returns 0 or -1. */
int vmm_clone_user_address_space(page_table_t *src, page_table_t *dst);

void vmm_page_fault_handler(void* frame);



#ifdef __cplusplus

}

#endif



#endif

