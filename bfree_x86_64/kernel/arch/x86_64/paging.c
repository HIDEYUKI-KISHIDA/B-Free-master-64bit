#include "paging.h"

#include <stddef.h>
#include <string.h>

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE   (1ULL << 1)
#define PTE_USER    (1ULL << 2)
#define PTE_PS      (1ULL << 7)
#define PAGE_2M     (2UL * 1024UL * 1024UL)

static uint64_t pml4[512] __attribute__((aligned(4096)));
static uint64_t pdpt[512] __attribute__((aligned(4096)));
static uint64_t pd[512] __attribute__((aligned(4096)));
static struct bfree_paging_state paging;

static int in_ring0(void)
{
	uint16_t cs;

	__asm__ volatile("mov %%cs, %0" : "=r"(cs));
	return (cs & 3U) == 0U;
}

void bfree_paging_build_identity(struct bfree_paging_state *st)
{
	size_t i;
	size_t pages = BFREE_IDENTITY_MAP_BYTES / PAGE_2M;

	if (st == NULL)
		return;

	memset(pml4, 0, sizeof(pml4));
	memset(pdpt, 0, sizeof(pdpt));
	memset(pd, 0, sizeof(pd));

	pml4[0] = (uint64_t)(uintptr_t)pdpt | PTE_PRESENT | PTE_WRITE;
	pdpt[0] = (uint64_t)(uintptr_t)pd | PTE_PRESENT | PTE_WRITE;
	for (i = 0; i < pages; i++)
		pd[i] = (uint64_t)(i * PAGE_2M) | PTE_PRESENT | PTE_WRITE |
			PTE_USER | PTE_PS;

	st->pml4 = pml4;
	st->pdpt = pdpt;
	st->pd = pd;
	st->cr3 = (uint64_t)(uintptr_t)pml4;
	st->built = 1;
}

int bfree_paging_install(const struct bfree_paging_state *st)
{
	if (st == NULL || !st->built)
		return -1;
	if (!in_ring0())
		return -2;

	__asm__ volatile("mov %0, %%cr3" : : "r"(st->cr3) : "memory");
	return 0;
}

const struct bfree_paging_state *bfree_paging_state(void)
{
	return &paging;
}
