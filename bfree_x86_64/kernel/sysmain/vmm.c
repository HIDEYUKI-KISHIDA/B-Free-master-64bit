#include "vmm.h"
#include "elf_loader.h"
#include "../include/tk/kernel.h"

void uart_puts(const char *);
void uart_puthex64(uint64_t val);
void *pmm_alloc(void);
void *pmm_alloc_pt_page(void);
void pmm_free(void *addr);

#define VMM_PHYS_IDENTITY_LIMIT ((uint64_t)PT_EMBEDDED_COUNT * 0x200000ULL)

static int vmm_pte_direct(uint64_t pt_phys)
{
    return (pt_phys != 0ULL && pt_phys < VMM_PHYS_IDENTITY_LIMIT) ? 1 : 0;
}



#ifndef BFREE_BOOT_DEBUG

#define BFREE_BOOT_DEBUG 0

#endif



#ifndef BFREE_ENFORCE_WX

#define BFREE_ENFORCE_WX 0

#endif



#define VMM_PTE_RW 0x002ULL

#define VMM_PTE_NX (1ULL << 63)

extern page_table_t kernel_page_table;



static int vmm_get_indices(uint64_t vaddr, uint64_t *pd_index, uint64_t *pt_index) {

    if (vaddr >= VMM_USER_VA_BYTES) {

        return -1;

    }

    *pd_index = (vaddr >> 21) & 0x1FFULL;

    *pt_index = (vaddr >> 12) & 0x1FFULL;

    return (*pd_index < PT_LEVEL_MAX) ? 0 : -1;

}



static int vmm_pte_store(uint64_t pt_phys, uint64_t pt_index, uint64_t val, int embedded)
{
    (void)embedded;
    if (vmm_pte_direct(pt_phys)) {
        ((pte_t *)(uintptr_t)pt_phys)[pt_index] = val;
        return 0;
    }
    return bfree_kernel_poke_phys(pt_phys + pt_index * 8ULL, &val, 8ULL);
}

static int vmm_pte_load(uint64_t pt_phys, uint64_t pt_index, uint64_t *val_out, int embedded)
{
    if (!val_out) {
        return -1;
    }
    (void)embedded;
    if (vmm_pte_direct(pt_phys)) {
        *val_out = ((pte_t *)(uintptr_t)pt_phys)[pt_index];
        return 0;
    }
    return bfree_kernel_peek_phys(pt_phys + pt_index * 8ULL, val_out, 8ULL);
}

static int vmm_zero_pt_page(uint64_t pt_phys)
{
    uint64_t i;

    if (!pt_phys) {
        return -1;
    }
    if (vmm_pte_direct(pt_phys)) {
        pte_t *slot = (pte_t *)(uintptr_t)pt_phys;
        for (i = 0; i < PTE_COUNT; ++i) {
            slot[i] = 0;
        }
        return 0;
    }
    return bfree_kernel_clear_phys(pt_phys, PAGE_SIZE);
}

static int vmm_init_identity_pt_slot(uint64_t pt_phys, uint64_t pd_index, int embedded)
{
    int i;

    if (!pt_phys) {
        return -1;
    }
    for (i = 0; i < PTE_COUNT; ++i) {
        uint64_t phys = (pd_index * (uint64_t)PTE_COUNT + (uint64_t)i) * PAGE_SIZE;
        uint64_t val = phys | 0x003ULL;

        if (vmm_pte_store(pt_phys, (uint64_t)i, val, embedded) != 0) {
            return -1;
        }
    }
    return 0;
}



static pte_t *vmm_pt_slot_existing(page_table_t *pt, uint64_t pd_index)

{

    if (!pt || pd_index >= PT_LEVEL_MAX) {

        return 0;

    }

    if (pd_index < PT_EMBEDDED_COUNT) {

        return &pt->pt[pd_index][0];

    }

    return pt->pt_ext[pd_index - PT_EMBEDDED_COUNT];

}



static pte_t *vmm_pt_slot(page_table_t *pt, uint64_t pd_index)

{

    pte_t *slot;



    if (!pt || pd_index >= PT_LEVEL_MAX) {

        return 0;

    }

    if (pd_index < PT_EMBEDDED_COUNT) {

        return &pt->pt[pd_index][0];

    }

    slot = pt->pt_ext[pd_index - PT_EMBEDDED_COUNT];

    if (!slot) {
        uint64_t pt_phys;

        if (pt == &kernel_page_table) {
            pt_phys = (uint64_t)(uintptr_t)pmm_alloc();
        } else {
            pt_phys = (uint64_t)(uintptr_t)pmm_alloc_pt_page();
        }
        if (!pt_phys) {
            return 0;
        }
        if (pt == &kernel_page_table) {
            if (vmm_init_identity_pt_slot(pt_phys, pd_index, 0) != 0) {
                return 0;
            }
        } else if (vmm_zero_pt_page(pt_phys) != 0) {
            return 0;
        }
        slot = (pte_t *)(uintptr_t)pt_phys;
        pt->pt_ext[pd_index - PT_EMBEDDED_COUNT] = slot;
    }

    return slot;

}



/* interrupt.S SAVE_ALL frame (see dump_exception_frame in interrupt_main.c). */

static uint64_t *bfree_pf_save_all_frame(void *regs_arg)

{

    uint64_t *regs = (uint64_t *)regs_arg;



    if (!regs) {

        return 0;

    }

    return (uint64_t *)(uintptr_t)regs[0];

}



// ページフォールト例外ハンドラ

void vmm_page_fault_handler(void* frame) {

    uint64_t cr2;

    uint64_t cr3;

    uint64_t *save = bfree_pf_save_all_frame(frame);

    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));

    uart_puts("[EXCEPTION] Page Fault CR2=");

    uart_puthex64(cr2);

    uart_puts("\n");



    if (save) {

        uint64_t rip = save[16];

        uint64_t cs = save[17];

        uint64_t rflags = save[18];

        uint64_t rsp = save[19];

        uint64_t ss = save[20];

        uint64_t errcode = save[15];



        uart_puts("[EXCEPTION] RIP=");

        uart_puthex64(rip);

        uart_puts(" CS=");

        uart_puthex64(cs);

        uart_puts(" RFLAGS=");

        uart_puthex64(rflags);

        uart_puts(" RSP=");

        uart_puthex64(rsp);

        uart_puts(" SS=");

        uart_puthex64(ss);

        uart_puts(" ERR=");

        uart_puthex64(errcode);

        uart_puts("\n");

        if (rsp != 0) {
            unsigned si;
            uart_puts("[EXCEPTION] GPR:");
            for (si = 0; si < 15u; ++si) {
                uart_puts(" r");
                uart_puthex64((uint64_t)si);
                uart_puts("=");
                uart_puthex64(save[si]);
            }
            uart_puts("\n");
        }

    }



    uint64_t *pml4 = (uint64_t*)(uintptr_t)(cr3 & ~0xFFFULL);

    uint64_t pml4_index = (cr2 >> 39) & 0x1FFULL;

    uint64_t pdpt_index = (cr2 >> 30) & 0x1FFULL;

    uint64_t pd_index = (cr2 >> 21) & 0x1FFULL;

    uint64_t pt_index = (cr2 >> 12) & 0x1FFULL;

    uint64_t pml4e = pml4[pml4_index];



    uart_puts("[EXCEPTION] CR3=");

    uart_puthex64(cr3);

    uart_puts(" PML4E=");

    uart_puthex64(pml4e);

    uart_puts("\n");



    if (pml4e & 0x001ULL) {

        uint64_t *pdpt = (uint64_t*)(uintptr_t)(pml4e & ~0xFFFULL);

        uint64_t pdpte = pdpt[pdpt_index];

        uart_puts("[EXCEPTION] PDPTE=");

        uart_puthex64(pdpte);

        uart_puts("\n");

        if (pdpte & 0x001ULL) {

            uint64_t *pd = (uint64_t*)(uintptr_t)(pdpte & ~0xFFFULL);

            uint64_t pde = pd[pd_index];

            uart_puts("[EXCEPTION] PDE=");

            uart_puthex64(pde);

            uart_puts("\n");

            if (pde & 0x001ULL) {

                uint64_t pt_phys = pde & ~0xFFFULL;
                uint64_t pte = 0;

                if (vmm_pte_direct(pt_phys)) {
                    pte = ((uint64_t *)(uintptr_t)pt_phys)[pt_index];
                } else {
                    (void)bfree_kernel_peek_phys(pt_phys + pt_index * 8ULL, &pte, 8ULL);
                }

                uart_puts("[EXCEPTION] PTE=");

                uart_puthex64(pte);

                uart_puts("\n");

            }

        }

    }



    while (1) { __asm__ volatile ("cli; hlt"); }

}



// 仮想アドレスvaddrに物理アドレスpaddrをflags付きでマッピング

int vmm_map_page(page_table_t *pt, uint64_t vaddr, uint64_t paddr, uint64_t flags) {

    uint64_t pd_index;

    uint64_t pt_index;

    uint64_t pd_flags = 0x003ULL;

    pte_t *pt_slot;



    if (vmm_get_indices(vaddr, &pd_index, &pt_index) != 0) {

        return -1;

    }

    pt_slot = vmm_pt_slot(pt, pd_index);

    if (!pt_slot) {

        return -1;

    }

    if (flags & 0x004ULL) {

        pd_flags |= 0x004ULL;

    }

    {

        uint64_t pte_flags = (flags & 0xFFFULL) | 0x001ULL;

#if BFREE_ENFORCE_WX

        if (pte_flags & VMM_PTE_RW) {

            pte_flags |= VMM_PTE_NX;

        }

#endif

        {
            uint64_t pte_val = (paddr & ~(PAGE_SIZE - 1)) | pte_flags;

            if (pd_index < PT_EMBEDDED_COUNT) {
                pt_slot[pt_index] = pte_val;
                if ((pt_slot[pt_index] & 0x001ULL) == 0ULL) {
                    return -1;
                }
            } else {
                uint64_t pt_phys = (uint64_t)(uintptr_t)pt_slot;
                if (vmm_pte_store(pt_phys, pt_index, pte_val, 0) != 0) {
                    return -1;
                }
                {
                    uint64_t verify = 0;
                    if (vmm_pte_load(pt_phys, pt_index, &verify, 0) != 0 ||
                        (verify & 0x001ULL) == 0ULL) {
                        return -1;
                    }
                }
            }
        }

    }

    pt->pd[pd_index] = ((uint64_t)(uintptr_t)pt_slot) | pd_flags;

    if (flags & 0x004ULL) {

        pt->pdpt[0] |= 0x004ULL;

        pt->pml4[0] |= 0x004ULL;

        __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");

    }

#if BFREE_BOOT_DEBUG

    uart_puts("[VMM][MAP] vaddr="); uart_puthex64(vaddr);

    uart_puts(" paddr="); uart_puthex64(paddr);

    uart_puts(" flags="); uart_puthex64(flags);

    uart_puts("\n");

#endif

    return 0;

}



// 仮想アドレスvaddrのマッピング解除

int vmm_unmap_page(page_table_t *pt, uint64_t vaddr) {

    uint64_t pd_index;

    uint64_t pt_index;

    pte_t *pt_slot;



    if (vmm_get_indices(vaddr, &pd_index, &pt_index) != 0) {

        return -1;

    }

    pt_slot = vmm_pt_slot_existing(pt, pd_index);

    if (!pt_slot) {

        return 0;

    }

    {
        uint64_t pt_phys = (uint64_t)(uintptr_t)pt_slot;
        int embedded = (pd_index < PT_EMBEDDED_COUNT) ? 1 : 0;
        uint64_t zero = 0;

        if (vmm_pte_store(pt_phys, pt_index, zero, embedded) != 0) {
            return -1;
        }
    }

#if BFREE_BOOT_DEBUG

    uart_puts("[VMM][UNMAP] vaddr="); uart_puthex64(vaddr);

    uart_puts("\n");

#endif

    return 0;

}



int vmm_user_page_mapped(page_table_t *pt, uint64_t vaddr)

{

    uint64_t pd_index;

    uint64_t pt_index;

    uint64_t pte;

    uint64_t paddr;

    pte_t *pt_slot;



    if (!pt) {

        return 0;

    }

    vaddr &= ~(PAGE_SIZE - 1ULL);

    if (vmm_get_indices(vaddr, &pd_index, &pt_index) != 0) {

        return 0;

    }

    pt_slot = vmm_pt_slot_existing(pt, pd_index);

    if (!pt_slot) {

        return 0;

    }

    {
        uint64_t pt_phys = (uint64_t)(uintptr_t)pt_slot;
        int embedded = (pd_index < PT_EMBEDDED_COUNT) ? 1 : 0;

        if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) != 0) {
            return 0;
        }
    }

    /* Clone leaves identity supervisor PTEs (Present|RW = 0x003). mmap/brk must

     * not treat those as guest heap/stack — user writes get ERR=7 (CR2 near RSP). */

    if ((pte & 0x007ULL) != 0x007ULL) {

        return 0;

    }

    paddr = pte & ~(PAGE_SIZE - 1ULL);

    if (paddr == vaddr) {

        return 0;

    }

    return 1;

}



int vmm_user_virt_to_phys(page_table_t *pt, uint64_t vaddr, uint64_t *paddr_out)
{
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t pte;
    uint64_t paddr;
    pte_t *pt_slot;

    if (!pt || !paddr_out) {
        return -1;
    }
    vaddr &= ~(PAGE_SIZE - 1ULL);
    if (vmm_get_indices(vaddr, &pd_index, &pt_index) != 0) {
        return -1;
    }
    pt_slot = vmm_pt_slot_existing(pt, pd_index);
    if (!pt_slot) {
        return -1;
    }
    {
        uint64_t pt_phys = (uint64_t)(uintptr_t)pt_slot;
        int embedded = (pd_index < PT_EMBEDDED_COUNT) ? 1 : 0;

        if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) != 0) {
            return -1;
        }
    }
    if ((pte & 0x007ULL) != 0x007ULL && (pte & 0x005ULL) != 0x005ULL) {
        return -1;
    }
    if ((pte & 0x001ULL) == 0ULL) {
        return -1;
    }
    paddr = pte & ~(PAGE_SIZE - 1ULL);
    if (paddr == vaddr) {
        return -1; /* identity clone */
    }
    *paddr_out = paddr;
    return 0;
}

void vmm_destroy_user_mappings(page_table_t *pt)
{
    uint64_t va;
    uint64_t pd_index;
    uint64_t pt_index;

    if (!pt) {
        return;
    }
    /* Walk only PD slots that may hold real user mappings; skip empty ones. */
    for (pd_index = 0; pd_index < (uint64_t)PT_LEVEL_MAX; ++pd_index) {
        pte_t *pt_slot = vmm_pt_slot_existing(pt, pd_index);

        if (!pt_slot) {
            continue;
        }
        for (pt_index = 0; pt_index < PTE_COUNT; ++pt_index) {
            uint64_t paddr = 0;
            uint64_t pte = 0;
            uint64_t pt_phys = (uint64_t)(uintptr_t)pt_slot;
            int embedded = (pd_index < PT_EMBEDDED_COUNT) ? 1 : 0;

            if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) != 0) {
                continue;
            }
            if ((pte & 0x001ULL) == 0ULL) {
                continue;
            }
            /* Present|User with either RW (0x007) or RO (0x005); skip identity. */
            if ((pte & 0x004ULL) == 0ULL) {
                continue;
            }
            paddr = pte & ~(PAGE_SIZE - 1ULL);
            va = (pd_index * 0x200000ULL) + (pt_index * PAGE_SIZE);
            if (paddr == va) {
                continue;
            }
            {
                uint64_t zero = 0;

                (void)vmm_pte_store(pt_phys, pt_index, zero, embedded);
            }
            pmm_free((void *)(uintptr_t)paddr);
        }
    }
}

int vmm_clone_user_address_space(page_table_t *src, page_table_t *dst)
{
    uint64_t pd_index;
    uint64_t pt_index;
    static uint8_t page_buf[PAGE_SIZE];

    if (!src || !dst) {
        return -1;
    }

    bfree_kernel_phys_io_begin();
    for (pd_index = 0; pd_index < (uint64_t)PT_LEVEL_MAX; ++pd_index) {
        pte_t *pt_slot = vmm_pt_slot_existing(src, pd_index);
        uint64_t pt_phys;
        int embedded;

        if (!pt_slot) {
            continue;
        }
        pt_phys = (uint64_t)(uintptr_t)pt_slot;
        embedded = (pd_index < PT_EMBEDDED_COUNT) ? 1 : 0;
        for (pt_index = 0; pt_index < PTE_COUNT; ++pt_index) {
            uint64_t pte = 0;
            uint64_t paddr;
            uint64_t va;
            uint64_t flags;
            void *newpage;

            if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) != 0) {
                continue;
            }
            if ((pte & 0x001ULL) == 0ULL) {
                continue;
            }
            if ((pte & 0x004ULL) == 0ULL) {
                continue; /* supervisor identity / kernel */
            }
            paddr = pte & ~(PAGE_SIZE - 1ULL);
            va = (pd_index * 0x200000ULL) + (pt_index * PAGE_SIZE);
            if (paddr == va) {
                continue;
            }
            if (bfree_kernel_peek_phys(paddr, page_buf, PAGE_SIZE) != 0) {
                bfree_kernel_phys_io_end();
                return -1;
            }
            newpage = pmm_alloc();
            if (!newpage) {
                bfree_kernel_phys_io_end();
                return -1;
            }
            if (bfree_kernel_poke_phys((uint64_t)(uintptr_t)newpage, page_buf, PAGE_SIZE) != 0) {
                pmm_free(newpage);
                bfree_kernel_phys_io_end();
                return -1;
            }
            flags = (pte & 0x007ULL) | (pte & (1ULL << 63));
            if (vmm_map_page(dst, va, (uint64_t)(uintptr_t)newpage, flags) != 0) {
                pmm_free(newpage);
                bfree_kernel_phys_io_end();
                return -1;
            }
        }
    }
    bfree_kernel_phys_io_end();
    return 0;
}



// カーネル用ページテーブルインスタンス

page_table_t kernel_page_table __attribute__((aligned(4096), section(".bss.page_table")));



/* PDPT[3] → 3–4 GiB MMIO (matches reset.S boot_pd_mmio layout). Shared by all clones. */

static pte_t kernel_mmio_pd[PTE_COUNT] __attribute__((aligned(4096)));



static void vmm_attach_mmio_pdpt(page_table_t *pt) {

    pt->pdpt[3] = ((uint64_t)(uintptr_t)kernel_mmio_pd) | 0x003ULL;

}



static void vmm_fix_pd_pointers(page_table_t *pt)

{

    int pd;



    for (pd = 0; pd < PT_EMBEDDED_COUNT; ++pd) {

        pt->pd[pd] = ((uint64_t)(uintptr_t)&pt->pt[pd][0]) | 0x003ULL;

    }

    for (pd = PT_EMBEDDED_COUNT; pd < PT_LEVEL_MAX; ++pd) {

        pte_t *slot = vmm_pt_slot_existing(pt, (uint64_t)pd);

        if (slot) {

            pt->pd[pd] = ((uint64_t)(uintptr_t)slot) | 0x003ULL;

        }

    }

}



// ページテーブル初期化

void vmm_init_kernel_page_table(void) {

    int pd;



    for (pd = 0; pd < PT_EMBEDDED_COUNT; ++pd) {

        vmm_init_identity_pt_slot((uint64_t)(uintptr_t)&kernel_page_table.pt[pd][0], (uint64_t)pd, 1);

    }

    for (pd = 0; pd < PT_EXT_COUNT; ++pd) {

        kernel_page_table.pt_ext[pd] = 0;

    }

    /* pt_ext for kernel is lazy; high phys uses loader staging + poke_phys. */

    vmm_fix_pd_pointers(&kernel_page_table);

    kernel_page_table.pdpt[0] = ((uint64_t)(uintptr_t)&kernel_page_table.pd[0]) | 0x003ULL;

    kernel_page_table.pml4[0] = ((uint64_t)(uintptr_t)&kernel_page_table.pdpt[0]) | 0x003ULL;

    vmm_attach_mmio_pdpt(&kernel_page_table);

    uart_puts("[VMM] init sparse-pt-v30 embed=");
    uart_puthex64(PT_EMBEDDED_COUNT);
    uart_puts(" max_pd=");
    uart_puthex64(PT_LEVEL_MAX);
    uart_puts("\n");

}



int vmm_map_mmio_huge(page_table_t *pt, uint64_t vaddr, uint64_t paddr) {

    uint64_t pdpt_i;

    uint64_t pd_i;



    if (!pt || vaddr < 0xC0000000ULL) {

        return -1;

    }

    pdpt_i = (vaddr >> 30) & 0x1FFULL;

    if (pdpt_i != 3ULL) {

        return -1;

    }

    pd_i = (vaddr >> 21) & 0x1FFULL;

    kernel_mmio_pd[pd_i] = (paddr & ~0x1FFFFFULL) | 0x083ULL; /* Present|RW|PS */

    vmm_attach_mmio_pdpt(pt);

    return 0;

}



void vmm_activate_kernel_page_table(void) {

    __asm__ volatile ("mov %0, %%cr3" :: "r"(&kernel_page_table) : "memory");

}



void vmm_clone_kernel_page_table(page_table_t *dst) {

    int ext_i;

    int pd;

    /* Copy only through embedded pt[] — not trailing BSS padding. */
    const uint64_t body_bytes =
        (uint64_t)((char *)&dst->pt_ext[0] - (char *)dst);



    if (!dst) {

        return;

    }

    /* Read source under kernel CR3 so a task-PT hole at 0x400000 cannot #PF
     * while memcpy'ing kernel_page_table (object may span that VA). */
    bfree_kernel_phys_io_begin();
    {
        uint64_t i;
        const uint64_t *src = (const uint64_t *)&kernel_page_table;
        uint64_t *d = (uint64_t *)dst;
        for (i = 0; i < body_bytes / sizeof(uint64_t); ++i) {
            d[i] = src[i];
        }
    }
    bfree_kernel_phys_io_end();

    for (ext_i = 0; ext_i < PT_EXT_COUNT; ++ext_i) {

        dst->pt_ext[ext_i] = 0;

    }

    for (pd = PT_EMBEDDED_COUNT; pd < PT_LEVEL_MAX; ++pd) {

        dst->pd[pd] = 0;

    }

    for (pd = 0; pd < PT_EMBEDDED_COUNT; ++pd) {

        /* Keep identity PTEs as Present|RW (0x003) — ring0 only. User mappings

         * (stack, ELF) are installed explicitly via vmm_map_page(..., 0x007). */

        dst->pd[pd] = ((uint64_t)(uintptr_t)&dst->pt[pd][0]) | 0x003ULL;

    }

    dst->pdpt[0] = ((uint64_t)(uintptr_t)&dst->pd[0]) | 0x003ULL;

    dst->pml4[0] = ((uint64_t)(uintptr_t)&dst->pdpt[0]) | 0x003ULL;

    /* MMIO window: same kernel_mmio_pd as kernel_page_table (pdpt[3]). */

    if (kernel_page_table.pdpt[3] & 0x001ULL) {

        dst->pdpt[3] = kernel_page_table.pdpt[3] | 0x007ULL;

    }

}



// デバッグ: ページテーブル内容をシリアル出力

void vmm_dump_kernel_page_table(void) {

    uart_puts("[VMM] PML4[0]="); uart_puthex64(kernel_page_table.pml4[0]); uart_puts("\n");

    uart_puts("[VMM] PDPT[0]="); uart_puthex64(kernel_page_table.pdpt[0]); uart_puts("\n");

    uart_puts("[VMM] PD[0]="); uart_puthex64(kernel_page_table.pd[0]); uart_puts("\n");

    for (int i = 0; i < 4; ++i) {

        uart_puts("[VMM] PT["); uart_puthex64(i);

        uart_puts("]="); uart_puthex64(kernel_page_table.pt[0][i]); uart_puts("\n");

    }

}

