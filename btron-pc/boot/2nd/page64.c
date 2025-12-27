/*

B-Free Project - GNU Generic PUBLIC LICENSE

64-bit paging management

*/

#include "types.h"
#include "location.h"
#include "config.h"
#include "page.h"
#include "memory64.h"
#include "lib.h"

/* 64-bit page tables */
static pml4e_t *pml4 = NULL;
static pde64_t *pml4_tables[512];

#define PT_PRESENT 0x001
#define PT_WRITABLE 0x002
#define PT_USER 0x004
#define PT_PWT 0x008
#define PT_PCD 0x010
#define PT_ACCESSED 0x020
#define PT_DIRTY 0x040
#define PT_LARGE_PAGE 0x080
#define PT_GLOBAL 0x100
#define PT_NX ((UWORD64)1 << 63)

/*
 * Initialize 64-bit paging structures
 * Creates identity mapping for the first 512GB of address space
 */
void
init_vm64(void)
{
	pml4e_t *pml4_entry;
	pde64_t *pde_entry;
	pte64_t *pte_entry;
	int i, j, k;
	UWORD64 phys_addr = 0;
	
	/* Initialize PML4 */
	pml4 = (pml4e_t *)PAGE_PML4_ADDR;
	
	/* Clear PML4 */
	for (i = 0; i < 512; i++) {
		pml4[i].present = 0;
	}
	
	/* Map lower 512GB (for initial boot and device memory) */
	for (i = 0; i < 512; i++) {
		pde64_t *pdpt = (pde64_t *)(PAGE_PDPT_ADDR + (i * 0x1000));
		
		/* Clear PDPT */
		for (j = 0; j < 512; j++) {
			pdpt[j].present = 0;
		}
		
		/* Create PDPT entries for 1GB pages */
		for (j = 0; j < 512; j++) {
			pde64_t *pdt = (pde64_t *)(PAGE_DIR_ADDR + ((i * 512 + j) * 0x1000));
			
			/* Clear PDT */
			for (k = 0; k < 512; k++) {
				pdt[k].present = 0;
			}
			
			/* Set PDPT entry to point to PDT */
			pdpt[j].present = 1;
			pdpt[j].read_write = 1;
			pdpt[j].user_supervisor = 0;
			pdpt[j].page_addr = (((UWORD64)pdt) >> 12) & 0xFFFFFFFFFF;
			
			/* Create page directory entries (2MB pages) */
			for (k = 0; k < 512; k++) {
				pdt[k].present = 1;
				pdt[k].read_write = 1;
				pdt[k].user_supervisor = 0;
				pdt[k].page_size = 1;	/* 2MB page */
				pdt[k].page_addr = ((phys_addr >> 21) << 9);
				phys_addr += 0x200000;	/* 2MB */
			}
		}
		
		/* Set PML4 entry */
		pml4[i].present = 1;
		pml4[i].read_write = 1;
		pml4[i].user_supervisor = 0;
		pml4[i].page_addr = (((UWORD64)pdpt) >> 12) & 0xFFFFFFFFFF;
	}
}

/*
 * Map virtual address to physical address in 64-bit mode
 */
int
map_vm64(UWORD64 raddr, UWORD64 vaddr, UWORD64 size)
{
	pml4e_t *pml4_entry;
	pde64_t *pdpt_entry, *pdt_entry;
	pte64_t *pt_entry;
	UWORD64 vaddr_aligned, size_aligned;
	int pml4_idx, pdpt_idx, pdt_idx, pt_idx;
	
	if (pml4 == NULL) {
		return -1;
	}
	
	/* Align addresses to page boundaries */
	vaddr_aligned = vaddr & ~(PAGE_SIZE - 1);
	size_aligned = ((size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
	
	while (size_aligned > 0) {
		/* Extract indices from virtual address */
		pml4_idx = (vaddr_aligned >> 39) & 0x1FF;
		pdpt_idx = (vaddr_aligned >> 30) & 0x1FF;
		pdt_idx = (vaddr_aligned >> 21) & 0x1FF;
		pt_idx = (vaddr_aligned >> 12) & 0x1FF;
		
		/* Check/create PML4 entry */
		pml4_entry = &pml4[pml4_idx];
		if (!pml4_entry->present) {
			UWORD64 pdpt_phys = (UWORD64)malloc64(PAGE_SIZE);
			if (pdpt_phys == 0) {
				return -1;
			}
			pml4_entry->present = 1;
			pml4_entry->read_write = 1;
			pml4_entry->user_supervisor = 0;
			pml4_entry->page_addr = (pdpt_phys >> 12) & 0xFFFFFFFFFF;
		}
		
		/* Check/create PDPT entry */
		pdpt_entry = (pde64_t *)(((UWORD64)pml4_entry->page_addr << 12) + pdpt_idx * sizeof(pde64_t));
		if (!pdpt_entry->present) {
			UWORD64 pdt_phys = (UWORD64)malloc64(PAGE_SIZE);
			if (pdt_phys == 0) {
				return -1;
			}
			pdpt_entry->present = 1;
			pdpt_entry->read_write = 1;
			pdpt_entry->user_supervisor = 0;
			pdpt_entry->page_addr = (pdt_phys >> 12) & 0xFFFFFFFFFF;
		}
		
		/* Check/create PDT entry */
		pdt_entry = (pde64_t *)(((UWORD64)pdpt_entry->page_addr << 12) + pdt_idx * sizeof(pde64_t));
		if (!pdt_entry->present) {
			UWORD64 pt_phys = (UWORD64)malloc64(PAGE_SIZE);
			if (pt_phys == 0) {
				return -1;
			}
			pdt_entry->present = 1;
			pdt_entry->read_write = 1;
			pdt_entry->user_supervisor = 0;
			pdt_entry->page_addr = (pt_phys >> 12) & 0xFFFFFFFFFF;
		}
		
		/* Set page table entry */
		pt_entry = (pte64_t *)(((UWORD64)pdt_entry->page_addr << 12) + pt_idx * sizeof(pte64_t));
		pt_entry->present = 1;
		pt_entry->read_write = 1;
		pt_entry->user_supervisor = 0;
		pt_entry->accessed = 0;
		pt_entry->dirty = 0;
		pt_entry->frame_addr = (raddr >> 12) & 0xFFFFFFFFFF;
		
		/* Move to next page */
		vaddr_aligned += PAGE_SIZE;
		raddr += PAGE_SIZE;
		size_aligned -= PAGE_SIZE;
	}
	
	return 0;
}

/*
 * Get page table entry for 64-bit address
 */
pte64_t *
get_page_entry64(UWORD64 addr)
{
	pml4e_t *pml4_entry;
	pde64_t *pdpt_entry, *pdt_entry;
	pte64_t *pt_entry;
	int pml4_idx, pdpt_idx, pdt_idx, pt_idx;
	
	if (pml4 == NULL) {
		return NULL;
	}
	
	/* Extract indices */
	pml4_idx = (addr >> 39) & 0x1FF;
	pdpt_idx = (addr >> 30) & 0x1FF;
	pdt_idx = (addr >> 21) & 0x1FF;
	pt_idx = (addr >> 12) & 0x1FF;
	
	/* Navigate page table hierarchy */
	pml4_entry = &pml4[pml4_idx];
	if (!pml4_entry->present) {
		return NULL;
	}
	
	pdpt_entry = (pde64_t *)(((UWORD64)pml4_entry->page_addr << 12) + pdpt_idx * sizeof(pde64_t));
	if (!pdpt_entry->present) {
		return NULL;
	}
	
	pdt_entry = (pde64_t *)(((UWORD64)pdpt_entry->page_addr << 12) + pdt_idx * sizeof(pde64_t));
	if (!pdt_entry->present) {
		return NULL;
	}
	
	pt_entry = (pte64_t *)(((UWORD64)pdt_entry->page_addr << 12) + pt_idx * sizeof(pte64_t));
	return pt_entry;
}
