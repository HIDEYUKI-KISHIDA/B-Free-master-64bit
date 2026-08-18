#include "vmm.h"
#include "elf_loader.h"
#include "../include/tk/kernel.h"

void uart_puts(const char *);
void uart_puthex64(uint64_t val);
void *pmm_alloc(void);
void *pmm_alloc_pt_page(void);
void pmm_free(void *addr);
int vmm_map_page(page_table_t *pt, uint64_t vaddr, uint64_t paddr, uint64_t flags);
void vmm_drop_identity_alias(page_table_t *pt, uint64_t phys);

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

/* Software-available PTE bit: page is write-protected for lazy COW after fork. */
#define VMM_PTE_COW (1ULL << 9)
/* Software bit: MAP_SHARED — stay RW across fork (no private COW break). */
#define VMM_PTE_SHARED (1ULL << 10)

#define VMM_COW_REFCNT_SLOTS 256

static struct {
    uint64_t phys;
    uint32_t refs;
} g_vmm_cow_refcnt[VMM_COW_REFCNT_SLOTS];

static int vmm_cow_ref_slot(uint64_t phys, int create)
{
    int free_i = -1;
    int i;

    for (i = 0; i < VMM_COW_REFCNT_SLOTS; ++i) {
        if (g_vmm_cow_refcnt[i].phys == phys)
            return i;
        if (free_i < 0 && g_vmm_cow_refcnt[i].refs == 0)
            free_i = i;
    }
    if (!create || free_i < 0)
        return -1;
    g_vmm_cow_refcnt[free_i].phys = phys;
    g_vmm_cow_refcnt[free_i].refs = 0;
    return free_i;
}

static void vmm_cow_ref_inc(uint64_t phys)
{
    int i;

    if (!phys)
        return;
    i = vmm_cow_ref_slot(phys, 1);
    if (i < 0)
        return;
    ++g_vmm_cow_refcnt[i].refs;
}

static void vmm_cow_ref_dec(uint64_t phys)
{
    int i;

    if (!phys)
        return;
    i = vmm_cow_ref_slot(phys, 0);
    if (i < 0)
        return;
    if (g_vmm_cow_refcnt[i].refs == 0)
        return;
    --g_vmm_cow_refcnt[i].refs;
    if (g_vmm_cow_refcnt[i].refs == 0) {
        g_vmm_cow_refcnt[i].phys = 0;
        pmm_free((void *)(uintptr_t)phys);
        uart_puts("[COW] ref free phys=");
        uart_puthex64(phys);
        uart_puts("\n");
    }
}

static void vmm_cow_share_phys(uint64_t phys)
{
    /* First COW share: parent mapping + child mapping. */
    if (vmm_cow_ref_slot(phys, 0) < 0)
        vmm_cow_ref_inc(phys);
    vmm_cow_ref_inc(phys);
}

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

static int vmm_cow_break_at(page_table_t *pt, uint64_t va, uint64_t old_pte)
{
    uint64_t old_phys = old_pte & 0x000FFFFFFFFFF000ULL;
    void *newpage;
    static uint8_t cow_page_buf[PAGE_SIZE];
    uint64_t i;

    if (!pt || (old_pte & VMM_PTE_COW) == 0ULL)
        return -1;
    newpage = pmm_alloc();
    if (!newpage)
        return -1;
    if (bfree_kernel_peek_phys(old_phys, cow_page_buf, PAGE_SIZE) != 0) {
        pmm_free(newpage);
        return -1;
    }
    if (bfree_kernel_poke_phys((uint64_t)(uintptr_t)newpage, cow_page_buf, PAGE_SIZE) != 0) {
        pmm_free(newpage);
        return -1;
    }
    if (vmm_map_page(pt, va, (uint64_t)(uintptr_t)newpage, 0x007ULL) != 0) {
        pmm_free(newpage);
        return -1;
    }
    vmm_cow_ref_dec(old_phys);
    vmm_drop_identity_alias(pt, (uint64_t)(uintptr_t)newpage);
    vmm_drop_identity_alias(&kernel_page_table, (uint64_t)(uintptr_t)newpage);
    __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
    uart_puts("[COW] break va=");
    uart_puthex64(va);
    uart_puts(" old=");
    uart_puthex64(old_phys);
    uart_puts(" new=");
    uart_puthex64((uint64_t)(uintptr_t)newpage);
    uart_puts("\n");
    (void)i;
    return 0;
}

void vmm_page_fault_handler(void* frame) {

    uint64_t cr2;

    uint64_t cr3;

    uint64_t *save = bfree_pf_save_all_frame(frame);

    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));

    /* Deep2: lazy COW — user write to present RO+COW page → private copy, resume. */
    if (save && ((save[17] & 3ULL) == 3ULL)) {
        uint64_t errcode = save[15];
        if ((errcode & 0x7ULL) == 0x7ULL) {
            page_table_t *pt = (page_table_t *)(uintptr_t)(cr3 & ~0xFFFULL);
            uint64_t pd_index = 0;
            uint64_t pt_index = 0;
            pte_t *pt_slot;
            uint64_t pte = 0;
            int embedded;

            if (vmm_get_indices(cr2, &pd_index, &pt_index) == 0) {
                pt_slot = vmm_pt_slot_existing(pt, pd_index);
                if (pt_slot) {
                    uint64_t pt_phys = (uint64_t)(uintptr_t)pt_slot;
                    embedded = (pd_index < PT_EMBEDDED_COUNT) ? 1 : 0;
                    if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) == 0 &&
                        (pte & VMM_PTE_COW) != 0ULL &&
                        (pte & 0x001ULL) != 0ULL) {
                        if (vmm_cow_break_at(pt, cr2 & ~0xFFFULL, pte) == 0)
                            return;
                    }
                }
            }
        }
    }

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

        /* FS_BASE / TLS: errno is typically *(FS:0)+0x34 for musl. */
        {
            uint32_t fs_lo = 0;
            uint32_t fs_hi = 0;
            uint64_t fsbase = 0;
            __asm__ volatile("rdmsr" : "=a"(fs_lo), "=d"(fs_hi) : "c"(0xC0000100u));
            fsbase = ((uint64_t)fs_hi << 32) | (uint64_t)fs_lo;
            uart_puts("[EXCEPTION] FS_BASE=");
            uart_puthex64(fsbase);
            uart_puts("\n");
        }

        /* Decode-aid: bytes at faulting RIP (temporarily clear SMAP). */
        {
            uint64_t cr4v = 0;
            __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4v));
            if ((cs & 0x3U) == 0x3U && rip < 0x0000800000000000ULL) {
                const volatile unsigned char *bp =
                    (const volatile unsigned char *)(uintptr_t)rip;
                unsigned bi;
                if (cr4v & (1ULL << 21)) {
                    __asm__ volatile("stac" ::: "memory", "cc");
                }
                uart_puts("[EXCEPTION] bytes@RIP=");
                for (bi = 0; bi < 8u; ++bi) {
                    uart_puthex64((uint64_t)bp[bi]);
                    uart_puts(bi + 1u < 8u ? " " : "\n");
                }
                if (cr4v & (1ULL << 21)) {
                    __asm__ volatile("clac" ::: "memory", "cc");
                }
            }
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

                /* PTE covering faulting RIP — empty text pages decode as add (%rax). */
                {
                    uint64_t rip_va = save ? save[16] : 0;
                    uint64_t rpd = (rip_va >> 21) & 0x1FFULL;
                    uint64_t rpt = (rip_va >> 12) & 0x1FFULL;
                    uint64_t rpde = pd[rpd];
                    uint64_t rpte = 0;
                    if ((rpde & 0x001ULL) != 0ULL) {
                        uint64_t rpt_phys = rpde & ~0xFFFULL;
                        if (vmm_pte_direct(rpt_phys)) {
                            rpte = ((uint64_t *)(uintptr_t)rpt_phys)[rpt];
                        } else {
                            (void)bfree_kernel_peek_phys(rpt_phys + rpt * 8ULL, &rpte, 8ULL);
                        }
                    }
                uart_puts("[EXCEPTION] RIP_PTE=");
                uart_puthex64(rpte);
                uart_puts("\n");
                if ((rpte & 0x001ULL) != 0ULL) {
                    uint64_t rphys = rpte & ~0xFFFULL;
                    uint8_t rchk[4];
                    if (bfree_kernel_peek_phys(rphys + (rip_va & 0xFFFULL), rchk, 4ULL) == 0) {
                        uart_puts("[EXCEPTION] RIP_PHYS_bytes=");
                        uart_puthex64((uint64_t)rchk[0]);
                        uart_puts(" ");
                        uart_puthex64((uint64_t)rchk[1]);
                        uart_puts(" ");
                        uart_puthex64((uint64_t)rchk[2]);
                        uart_puts(" ");
                        uart_puthex64((uint64_t)rchk[3]);
                        uart_puts(" phys=");
                        uart_puthex64(rphys);
                        uart_puts("\n");
                    }
                }
                }

            }

        }

    }

    /*
     * Survive demo: user-mode (#CPL3) PF parks the Linux ABI / Qt guest but
     * MUST NOT cli;hlt the whole machine — IRQ0 timer_handler keeps printing
     * [BFreeCore] tick=… as structural proof that the RTOS core outlives the
     * compatibility persona.
     */
    if (save && ((save[17] & 3ULL) == 3ULL)) {
        uart_puts("[SURVIVE] user-mode Page Fault — parking guest ABI\n");
        uart_puts("[SURVIVE] expect BFreeCore tick to continue\n");
        uart_puts("[SURVIVE] demo ok\n");
        for (;;) {
            __asm__ volatile ("sti; hlt" ::: "memory");
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

    }

    /* Always shoot down this VA: supervisor remaps (loader staging) need it too. */
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");

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
        uint64_t pte = 0;
        uint64_t zero = 0;

        if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) == 0 &&
            (pte & 0x001ULL) != 0ULL && (pte & 0x004ULL) != 0ULL &&
            ((pte & VMM_PTE_COW) != 0ULL || (pte & VMM_PTE_SHARED) != 0ULL)) {
            vmm_cow_ref_dec(pte & 0x000FFFFFFFFFF000ULL);
        }
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

void vmm_drop_identity_alias(page_table_t *pt, uint64_t phys)
{
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t pte = 0;
    pte_t *pt_slot;
    uint64_t pt_phys;
    int embedded;

    if (!pt) {
        return;
    }
    phys &= ~(PAGE_SIZE - 1ULL);
    if (phys == 0ULL || phys >= VMM_USER_VA_BYTES) {
        return;
    }
    if (vmm_get_indices(phys, &pd_index, &pt_index) != 0) {
        return;
    }
    pt_slot = vmm_pt_slot_existing(pt, pd_index);
    if (!pt_slot) {
        return;
    }
    pt_phys = (uint64_t)(uintptr_t)pt_slot;
    embedded = (pd_index < PT_EMBEDDED_COUNT) ? 1 : 0;
    if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) != 0) {
        return;
    }
    /* Only supervisor identity Present|RW, VA==PA (no User bit). */
    if ((pte & 0x001ULL) == 0ULL) {
        return;
    }
    if ((pte & 0x004ULL) != 0ULL) {
        return;
    }
    if ((pte & ~(PAGE_SIZE - 1ULL)) != phys) {
        return;
    }
    (void)vmm_unmap_page(pt, phys);
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

int vmm_user_maps_phys(page_table_t *pt, uint64_t paddr)
{
    uint64_t pd_index;
    uint64_t pt_index;

    if (!pt || paddr == 0) {
        return 0;
    }
    paddr &= ~(PAGE_SIZE - 1ULL);
    for (pd_index = 0; pd_index < (uint64_t)PT_LEVEL_MAX; ++pd_index) {
        pte_t *pt_slot = vmm_pt_slot_existing(pt, pd_index);
        uint64_t pt_phys;
        int embedded;

        if (!pt_slot) {
            continue;
        }
        pt_phys = (uint64_t)(uintptr_t)pt_slot;
        embedded = (pd_index < PT_EMBEDDED_COUNT) ? 1 : 0;
        for (pt_index = 0; pt_index < PTE_COUNT; ++pt_index) {
            uint64_t pte = 0;
            uint64_t pp;

            if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) != 0) {
                continue;
            }
            if ((pte & 0x005ULL) != 0x005ULL && (pte & 0x007ULL) != 0x007ULL) {
                continue;
            }
            pp = pte & ~(PAGE_SIZE - 1ULL);
            if (pp == paddr) {
                return 1;
            }
        }
    }
    return 0;
}

void vmm_destroy_user_mappings(page_table_t *pt)
{
    uint64_t va;
    uint64_t pd_index;
    uint64_t pt_index;
    extern uint64_t g_bfree_shell_text_phys;
    extern int bfree_shell_page_pinned(uint64_t phys);

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
            /* Present|User with either RW (0x007) or RO (0x005); skip identity
             * only when the mapping is supervisor (no User bit). */
            if ((pte & 0x004ULL) == 0ULL) {
                continue;
            }
            paddr = pte & ~(PAGE_SIZE - 1ULL);
            va = (pd_index * 0x200000ULL) + (pt_index * PAGE_SIZE);
            {
                uint64_t zero = 0;

                (void)vmm_pte_store(pt_phys, pt_index, zero, embedded);
            }
            /* Identity-backed user pages were never pmm_alloc'd — do not free. */
            if (paddr == va) {
                continue;
            }
            /* Never free sticky / pinned shell image frames. */
            if ((g_bfree_shell_text_phys != 0 && paddr == g_bfree_shell_text_phys) ||
                bfree_shell_page_pinned(paddr)) {
                continue;
            }
            /* Tree2/Compat1: COW or SHARED shared phys — drop one ref. */
            if ((pte & VMM_PTE_COW) != 0ULL || (pte & VMM_PTE_SHARED) != 0ULL) {
                uart_puts("[COW] exit drop phys=");
                uart_puthex64(paddr);
                uart_puts("\n");
                vmm_cow_ref_dec(paddr);
                continue;
            }
            pmm_free((void *)(uintptr_t)paddr);
        }
    }
}

void vmm_destroy_user_mappings_keep(page_table_t *pt, page_table_t *keep)
{
    uint64_t va;
    uint64_t pd_index;
    uint64_t pt_index;
    extern uint64_t g_bfree_shell_text_phys;
    extern int bfree_shell_page_pinned(uint64_t phys);
    /* One-shot parent phys set — avoids O(n·m) vmm_user_maps_phys per page. */
    enum { KEEP_HASH = 16384 };
    static uint64_t keep_hash[KEEP_HASH];
    uint64_t hi;

    if (!pt) {
        return;
    }
    if (!keep) {
        vmm_destroy_user_mappings(pt);
        return;
    }
    for (hi = 0; hi < (uint64_t)KEEP_HASH; ++hi) {
        keep_hash[hi] = 0;
    }
    for (pd_index = 0; pd_index < (uint64_t)PT_LEVEL_MAX; ++pd_index) {
        pte_t *pt_slot = vmm_pt_slot_existing(keep, pd_index);
        uint64_t pt_phys;
        int embedded;

        if (!pt_slot) {
            continue;
        }
        pt_phys = (uint64_t)(uintptr_t)pt_slot;
        embedded = (pd_index < PT_EMBEDDED_COUNT) ? 1 : 0;
        for (pt_index = 0; pt_index < PTE_COUNT; ++pt_index) {
            uint64_t pte = 0;
            uint64_t pp;
            uint64_t slot;
            uint64_t probe;

            if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) != 0) {
                continue;
            }
            if ((pte & 0x005ULL) != 0x005ULL && (pte & 0x007ULL) != 0x007ULL) {
                continue;
            }
            pp = pte & ~(PAGE_SIZE - 1ULL);
            if (pp == 0) {
                continue;
            }
            slot = (pp >> 12) & (uint64_t)(KEEP_HASH - 1);
            for (probe = 0; probe < 32ULL; ++probe) {
                uint64_t i = (slot + probe) & (uint64_t)(KEEP_HASH - 1);
                if (keep_hash[i] == 0 || keep_hash[i] == pp) {
                    keep_hash[i] = pp;
                    break;
                }
            }
        }
    }
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
            int in_keep = 0;
            uint64_t slot;
            uint64_t probe;

            if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) != 0) {
                continue;
            }
            if ((pte & 0x001ULL) == 0ULL) {
                continue;
            }
            if ((pte & 0x004ULL) == 0ULL) {
                continue;
            }
            paddr = pte & ~(PAGE_SIZE - 1ULL);
            va = (pd_index * 0x200000ULL) + (pt_index * PAGE_SIZE);
            {
                uint64_t zero = 0;

                (void)vmm_pte_store(pt_phys, pt_index, zero, embedded);
            }
            if (paddr == va) {
                continue;
            }
            if ((g_bfree_shell_text_phys != 0 && paddr == g_bfree_shell_text_phys) ||
                bfree_shell_page_pinned(paddr)) {
                continue;
            }
            /* COW/SHARED: always drop child ref even when parent keeps the phys. */
            if ((pte & VMM_PTE_COW) != 0ULL || (pte & VMM_PTE_SHARED) != 0ULL) {
                uart_puts("[COW] exit drop phys=");
                uart_puthex64(paddr);
                uart_puts("\n");
                vmm_cow_ref_dec(paddr);
                continue;
            }
            slot = (paddr >> 12) & (uint64_t)(KEEP_HASH - 1);
            for (probe = 0; probe < 32ULL; ++probe) {
                uint64_t i = (slot + probe) & (uint64_t)(KEEP_HASH - 1);
                if (keep_hash[i] == 0) {
                    break;
                }
                if (keep_hash[i] == paddr) {
                    in_keep = 1;
                    break;
                }
            }
            if (in_keep) {
                continue;
            }
            /* Hash miss: exact walk fallback (rare collisions / overflow). */
            if (vmm_user_maps_phys(keep, paddr)) {
                continue;
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

    /*
     * Source: staging peek of the PTE frame (what the ELF loader wrote).
     * Dest: CR3-VA store under a temporary RW PTE (WP-safe). Staging destination
     * pokes previously disagreed with later guest walks; source peeks of ELF
     * frames have matched post-load verify.
     */
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
            uint64_t saved_cr3 = 0;
            uint64_t rflags = 0;
            uint64_t cr4v = 0;
            volatile uint8_t *vp;
            uint64_t i;

            if (vmm_pte_load(pt_phys, pt_index, &pte, embedded) != 0) {
                continue;
            }
            if ((pte & 0x001ULL) == 0ULL) {
                continue;
            }
            if ((pte & 0x004ULL) == 0ULL) {
                continue; /* supervisor identity / kernel */
            }
            paddr = pte & 0x000FFFFFFFFFF000ULL;
            va = (pd_index * 0x200000ULL) + (pt_index * PAGE_SIZE);
            if (paddr == va && (pte & 0x004ULL) == 0ULL) {
                continue;
            }

            /* Compat1: MAP_SHARED stays RW+shared across fork (no private COW). */
            if ((pte & VMM_PTE_SHARED) != 0ULL && (pte & 0x007ULL) == 0x007ULL) {
                uint64_t sh_flags = 0x007ULL | VMM_PTE_SHARED | (pte & VMM_PTE_NX);
                if (vmm_map_page(src, va, paddr, sh_flags) != 0)
                    return -1;
                if (vmm_map_page(dst, va, paddr, sh_flags) != 0)
                    return -1;
                vmm_cow_share_phys(paddr);
                vmm_drop_identity_alias(dst, paddr);
                uart_puts("[SHARED] fork keep va=");
                uart_puthex64(va);
                uart_puts("\n");
                continue;
            }

            /*
             * Already RO+COW (e.g. parent after a prior fork whose child exited
             * without breaking this page): must re-share with COW bit intact.
             * Falling through to eager copy produced child RO pages *without*
             * VMM_PTE_COW → TLS write PF (fcntl / fork-thrice).
             */
            if ((pte & VMM_PTE_COW) != 0ULL && (pte & 0x001ULL) != 0ULL &&
                (pte & 0x004ULL) != 0ULL) {
                uint64_t cow_flags = 0x005ULL | VMM_PTE_COW | (pte & VMM_PTE_NX);

                if (vmm_map_page(src, va, paddr, cow_flags) != 0)
                    return -1;
                if (vmm_map_page(dst, va, paddr, cow_flags) != 0)
                    return -1;
                /* Parent already holds one ref; add the new child sharer. */
                vmm_cow_ref_inc(paddr);
                vmm_drop_identity_alias(dst, paddr);
                continue;
            }

            /* Deep2 lazy COW: share RW user pages as RO+COW (break on write PF). */
            if ((pte & 0x007ULL) == 0x007ULL) {
                uint64_t cow_flags = 0x005ULL | VMM_PTE_COW | (pte & VMM_PTE_NX);
                if (vmm_map_page(src, va, paddr, cow_flags) != 0)
                    return -1;
                if (vmm_map_page(dst, va, paddr, cow_flags) != 0)
                    return -1;
                vmm_cow_share_phys(paddr);
                vmm_drop_identity_alias(dst, paddr);
                continue;
            }

            newpage = pmm_alloc();
            if (!newpage) {
                return -1;
            }
            flags = (pte & 0x007ULL) | (pte & (1ULL << 63));
            /* Map RW while copying so ring0 can store into RO text pages. */
            if (vmm_map_page(dst, va, (uint64_t)(uintptr_t)newpage, 0x007ULL) != 0) {
                pmm_free(newpage);
                return -1;
            }
            vmm_drop_identity_alias(dst, (uint64_t)(uintptr_t)newpage);
            vmm_drop_identity_alias(&kernel_page_table,
                                    (uint64_t)(uintptr_t)newpage);

            /* Prefer source CR3-VA (what the guest executes). Staging peeks of
             * the same phys have disagreed after shell init (setvbuf page). */
            {
                uint64_t saved_cr3_rd = 0;
                uint64_t rflags_rd = 0;
                uint64_t cr4_rd = 0;
                volatile uint8_t *vp_rd;
                uint64_t j;

                __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags_rd) : : "memory");
                __asm__ volatile("mov %%cr4, %0" : "=r"(cr4_rd));
                __asm__ volatile("mov %%cr3, %0" : "=r"(saved_cr3_rd) : : "memory");
                __asm__ volatile("mov %0, %%cr3" :: "r"(src) : "memory");
                if (cr4_rd & (1ULL << 21)) {
                    __asm__ volatile("stac" ::: "memory", "cc");
                }
                vp_rd = (volatile uint8_t *)(uintptr_t)va;
                for (j = 0; j < PAGE_SIZE; ++j) {
                    page_buf[j] = vp_rd[j];
                }
                if (cr4_rd & (1ULL << 21)) {
                    __asm__ volatile("clac" ::: "memory", "cc");
                }
                __asm__ volatile("mov %0, %%cr3" :: "r"(saved_cr3_rd) : "memory");
                if (rflags_rd & 0x200ULL) {
                    __asm__ volatile("sti" ::: "memory");
                }
            }
            if (va == 0x522000ULL) {
                uint8_t stg[4];
                bfree_kernel_phys_io_begin();
                (void)bfree_kernel_peek_phys(paddr + 0xEA0ULL, stg, 4ULL);
                bfree_kernel_phys_io_end();
                uart_puts("[FORK] va_vs_stg 0x522ea0 va=");
                uart_puthex64((uint64_t)page_buf[0xEA0]);
                uart_puts(" ");
                uart_puthex64((uint64_t)page_buf[0xEA1]);
                uart_puts(" ");
                uart_puthex64((uint64_t)page_buf[0xEA2]);
                uart_puts(" ");
                uart_puthex64((uint64_t)page_buf[0xEA3]);
                uart_puts(" stg=");
                uart_puthex64((uint64_t)stg[0]);
                uart_puts(" ");
                uart_puthex64((uint64_t)stg[1]);
                uart_puts(" ");
                uart_puthex64((uint64_t)stg[2]);
                uart_puts(" ");
                uart_puthex64((uint64_t)stg[3]);
                uart_puts(" src_phys=");
                uart_puthex64(paddr);
                uart_puts("\n");
            }

            __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags) : : "memory");
            __asm__ volatile("mov %%cr4, %0" : "=r"(cr4v));
            __asm__ volatile("mov %%cr3, %0" : "=r"(saved_cr3) : : "memory");

            /* Write into destination VA under dst CR3. */
            __asm__ volatile("mov %0, %%cr3" :: "r"(dst) : "memory");
            if (cr4v & (1ULL << 21)) {
                __asm__ volatile("stac" ::: "memory", "cc");
            }
            vp = (volatile uint8_t *)(uintptr_t)va;
            for (i = 0; i < PAGE_SIZE; ++i) {
                vp[i] = page_buf[i];
            }
            if (cr4v & (1ULL << 21)) {
                __asm__ volatile("clac" ::: "memory", "cc");
            }

            __asm__ volatile("mov %0, %%cr3" :: "r"(saved_cr3) : "memory");
            if (rflags & 0x200ULL) {
                __asm__ volatile("sti" ::: "memory");
            }

            /* Restore destination PTE flags (RO text must stay RO). */
            if ((flags & 0x007ULL) != 0x007ULL) {
                if (vmm_map_page(dst, va, (uint64_t)(uintptr_t)newpage, flags) != 0) {
                    pmm_free(newpage);
                    return -1;
                }
            }

            if (va == 0x522000ULL) {
                uart_puts("[FORK] clone 0x522ea0 bytes=");
                uart_puthex64((uint64_t)page_buf[0xEA0]);
                uart_puts(" ");
                uart_puthex64((uint64_t)page_buf[0xEA1]);
                uart_puts(" ");
                uart_puthex64((uint64_t)page_buf[0xEA2]);
                uart_puts(" ");
                uart_puthex64((uint64_t)page_buf[0xEA3]);
                uart_puts(" src_phys=");
                uart_puthex64(paddr);
                uart_puts(" dst_phys=");
                uart_puthex64((uint64_t)(uintptr_t)newpage);
                uart_puts("\n");
                /*
                 * Shell fingerprint at 0x522ea0 — diagnostic only. Curated (and
                 * any guest whose .text no longer covers that offset) must not
                 * abort AS-copy on mismatch.
                 */
                {
                    extern uint8_t g_bfree_shell_text_fp[4];
                    extern int g_bfree_shell_text_fp_valid;

                    if (g_bfree_shell_text_fp_valid &&
                        (page_buf[0xEA0] != g_bfree_shell_text_fp[0] ||
                         page_buf[0xEA1] != g_bfree_shell_text_fp[1] ||
                         page_buf[0xEA2] != g_bfree_shell_text_fp[2] ||
                         page_buf[0xEA3] != g_bfree_shell_text_fp[3])) {
                        uart_puts(
                            "[FORK] WARN: 0x522ea0 fingerprint mismatch (non-fatal)\n");
                    }
                }
            }
        }
    }
    return 0;
}



// カーネル用ページテーブルインスタンス

/* Diagnostic only. 0 = skip fork fingerprint. Defined here so --no-undefined links. */
uint8_t g_bfree_shell_text_fp[4];
int g_bfree_shell_text_fp_valid;

page_table_t kernel_page_table __attribute__((aligned(4096), section(".bfree_page_table")));



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

