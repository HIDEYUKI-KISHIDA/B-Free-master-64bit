// elf_loader.c - 簡易ELFローダ雛形
#include "elf_loader.h"
#include "vmm.h"
#include "../include/tk/kernel.h"
#include <stdint.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

// --- プロトタイプ宣言 ---
static int my_strcmp(const char *a, const char *b);
void *pmm_alloc(void);
void pmm_reserve_range(uint64_t base, uint64_t len, const char *label);
void uart_puts(const char *s);
void uart_puthex64(uint64_t val);
void initrd_register(const char *name, uint8_t *data, uint64_t size);

/* Phys of busybox pages covering 0x521fa0 / 0x522ea0 — watch for clobber. */
uint64_t g_bfree_elf_watch_phys;
uint64_t g_bfree_elf_watch_phys2;
/* Sticky: first shell busybox setvbuf frame — never cleared across child loads. */
uint64_t g_bfree_shell_text_phys;

#define BFREE_SHELL_PIN_MAX 384
static uint64_t g_bfree_shell_pin[BFREE_SHELL_PIN_MAX];
static int g_bfree_shell_pin_count;
static int g_bfree_shell_pin_done;

void bfree_shell_pin_page(uint64_t phys)
{
    uint64_t page = phys & ~(PAGE_SIZE - 1ULL);
    int i;

    if (page == 0) {
        return;
    }
    for (i = 0; i < g_bfree_shell_pin_count; ++i) {
        if (g_bfree_shell_pin[i] == page) {
            return;
        }
    }
    if (g_bfree_shell_pin_count >= BFREE_SHELL_PIN_MAX) {
        return;
    }
    g_bfree_shell_pin[g_bfree_shell_pin_count++] = page;
}

void bfree_shell_pin_range(page_table_t *pt, uint64_t va_lo, uint64_t va_hi)
{
    uint64_t va;

    if (!pt || va_hi <= va_lo) {
        return;
    }
    va_lo &= ~(PAGE_SIZE - 1ULL);
    va_hi = (va_hi + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    for (va = va_lo; va < va_hi; va += PAGE_SIZE) {
        uint64_t phys = 0;

        if (vmm_user_virt_to_phys(pt, va, &phys) == 0) {
            bfree_shell_pin_page(phys);
        }
    }
}

int bfree_shell_page_pinned(uint64_t phys)
{
    uint64_t page = phys & ~(PAGE_SIZE - 1ULL);
    int i;

    if (page == 0) {
        return 0;
    }
    if (g_bfree_shell_text_phys != 0 && page == g_bfree_shell_text_phys) {
        return 1;
    }
    for (i = 0; i < g_bfree_shell_pin_count; ++i) {
        if (g_bfree_shell_pin[i] == page) {
            return 1;
        }
    }
    return 0;
}

int bfree_shell_text_check(const char *tag)
{
    uint8_t chk[8];
    uint64_t phys = g_bfree_shell_text_phys;

    if (phys == 0) {
        uart_puts("[SHELL] ");
        if (tag) {
            uart_puts(tag);
        }
        uart_puts(" watch unset\n");
        return -1;
    }
    if (bfree_kernel_peek_phys(phys + 0xEA0ULL, chk, 8ULL) != 0) {
        uart_puts("[SHELL] ");
        if (tag) {
            uart_puts(tag);
        }
        uart_puts(" peek fail phys=");
        uart_puthex64(phys);
        uart_puts("\n");
        return -1;
    }
    uart_puts("[SHELL] ");
    if (tag) {
        uart_puts(tag);
    }
    uart_puts(" 0x522ea0=");
    uart_puthex64((uint64_t)chk[0]);
    uart_puts(" ");
    uart_puthex64((uint64_t)chk[1]);
    uart_puts(" ");
    uart_puthex64((uint64_t)chk[2]);
    uart_puts(" ");
    uart_puthex64((uint64_t)chk[3]);
    uart_puts(" ");
    uart_puthex64((uint64_t)chk[4]);
    uart_puts(" ");
    uart_puthex64((uint64_t)chk[5]);
    uart_puts(" ");
    uart_puthex64((uint64_t)chk[6]);
    uart_puts(" ");
    uart_puthex64((uint64_t)chk[7]);
    uart_puts(" phys=");
    uart_puthex64(phys);
    uart_puts("\n");
    if (chk[0] != 0xf3u || chk[1] != 0x0fu || chk[2] != 0x1eu || chk[3] != 0xfau) {
        return -2;
    }
    return 0;
}

#ifndef BFREE_BOOT_DEBUG
#define BFREE_BOOT_DEBUG 0
#endif

#if defined(__cplusplus)
}
#endif

// ELFヘッダ構造体（32bit/64bit混在対策のため最小限）
typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

// --- テスト用ELFバイナリ（user_hello.elf）をC配列として埋め込む ---
extern const unsigned char user_hello_elf[];
extern const unsigned int user_hello_elf_size;

// ---------------------------------------------------------------------------
// Multiboot2 modules (initrd) からファイルを検索するテーブル
// knl_main() が mb2_info からモジュールを登録する
// ---------------------------------------------------------------------------
#define INITRD_FILES_MAX 8

typedef struct {
    char     name[64];
    uint8_t *data;       /* mapped VA after register */
    uint64_t size;
    uint64_t phys_base;  /* Multiboot mod_start */
} initrd_file_t;

static initrd_file_t _initrd_files[INITRD_FILES_MAX];
static int           _initrd_count = 0;

static bfree_loaded_elf_info_t g_last_loaded_elf;

void bfree_loaded_elf_info_get(bfree_loaded_elf_info_t *out)
{
    if (out) {
        *out = g_last_loaded_elf;
    }
}

extern page_table_t kernel_page_table;

/* Initrd read window: must stay below PT_LEVEL_COUNT*2MiB (128 MiB).
 * Above guest desktop load (0x02800000 + ~34 MiB); leaves room for two ~34 MiB aliases. */
#define BFREE_INITRD_MAP_BASE 0x05000000ULL
#define BFREE_INITRD_MAP_LIMIT (PT_LEVEL_COUNT * 0x200000ULL)
static uint64_t g_initrd_map_next = BFREE_INITRD_MAP_BASE;

// kernel (main.c) から呼ぶ: Multiboot2 module タグ (type=3) を登録
void initrd_register(const char *name, uint8_t *data, uint64_t size) {
    uint64_t phys_base;
    uint64_t vbase;
    uint64_t off;
    int i;
    int j;

    if (_initrd_count >= INITRD_FILES_MAX) return;
    if (!data || size == 0) return;

    phys_base = (uint64_t)(uintptr_t)data;

    for (j = 0; j < _initrd_count; ++j) {
        if (_initrd_files[j].phys_base == phys_base) {
            /* Same Multiboot module — do not double-reserve PMM or remap VA. */
            if (_initrd_files[j].size == size) {
                initrd_file_t *f = &_initrd_files[_initrd_count++];
                i = 0;
                while (name[i] && i < 63) { f->name[i] = name[i]; ++i; }
                f->name[i] = '\0';
                f->data = _initrd_files[j].data;
                f->size = size;
                f->phys_base = phys_base;
                uart_puts("[INITRD] alias ");
                uart_puts(name);
                uart_puts(" -> va=");
                uart_puthex64((uint64_t)(uintptr_t)f->data);
                uart_puts("\n");
                return;
            }
        }
    }

    /* desktop.elf (~34 MiB) is loaded via read_file from mod_start while PT_LOAD
     * pages come from pmm_alloc(). Without reserving the module, alloc overwrites
     * initrd before high rodata offsets are read (garbage serial / #UD in Qt). */
    pmm_reserve_range(phys_base, size, name);

    vbase = g_initrd_map_next;
    if (vbase + size > BFREE_INITRD_MAP_LIMIT) {
        /* Qt desktop.elf (~50 MiB on disk) exceeds the initrd VA window
         * (0x05000000..PT_LEVEL_COUNT*2MiB). read_file() loads via phys_base
         * (Multiboot mod_start, identity-mapped low RAM), so registration
         * without a kernel VA alias is sufficient for exec_initrd. */
        uart_puts("[INITRD] phys-only (VA window full) name=");
        uart_puts(name);
        uart_puts(" phys=");
        uart_puthex64(phys_base);
        uart_puts(" size=");
        uart_puthex64(size);
        uart_puts("\n");
        vbase = 0;
    } else {
        g_initrd_map_next = (g_initrd_map_next + size + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);

        uart_puts("[INITRD] map ");
        uart_puts(name);
        uart_puts(" phys=");
        uart_puthex64(phys_base);
        uart_puts(" va=");
        uart_puthex64(vbase);
        uart_puts(" size=");
        uart_puthex64(size);
        uart_puts("\n");

        for (off = 0; off < size; off += PAGE_SIZE) {
            if (vmm_map_page(&kernel_page_table, vbase + off, phys_base + off, 0x003ULL) != 0) {
                uart_puts("[INITRD] map failed va=");
                uart_puthex64(vbase + off);
                uart_puts(" (limit ");
                uart_puthex64(BFREE_INITRD_MAP_LIMIT);
                uart_puts(") — phys-only fallback\n");
                vbase = 0;
                break;
            }
        }
    }

    {
        initrd_file_t *f = &_initrd_files[_initrd_count++];
        i = 0;
        while (name[i] && i < 63) { f->name[i] = name[i]; ++i; }
        f->name[i] = '\0';
        f->data = vbase ? (uint8_t *)(uintptr_t)vbase : 0;
        f->size = size;
        f->phys_base = phys_base;
    }
}

void initrd_sync_user_page_tables(void) {
    extern TCB tcb1;
    extern TCB tcb2;

    if (tcb1.page_table_base) {
        vmm_clone_kernel_page_table((page_table_t *)tcb1.page_table_base);
    }
    if (tcb2.page_table_base) {
        vmm_clone_kernel_page_table((page_table_t *)tcb2.page_table_base);
    }
    /* initrd VA (0x05000000+) is mapped only in kernel_page_table; boot PT covers 16 MiB. */
    vmm_activate_kernel_page_table();
    uart_puts("[INITRD] page tables synced (initrd VA window)\n");
}

// ---------------------------------------------------------------------------
// read_file: C配列バイナリ + initrd 登録ファイルの両方に対応
// ---------------------------------------------------------------------------
int read_file(const char *filename, uint64_t offset, void *buf, uint64_t size) {
    if (!filename) return -1;

    // 1) C配列フォールバック（user_hello.elf）
    if (my_strcmp(filename, "user_hello.elf") == 0) {
        if (offset + size > user_hello_elf_size) return -2;
        const unsigned char *src = user_hello_elf + offset;
        unsigned char *dst = (unsigned char*)buf;
        for (uint64_t i = 0; i < size; ++i) dst[i] = src[i];
        return 0;
    }

    // 2) initrd 登録済みファイル
    for (int i = 0; i < _initrd_count; ++i) {
        if (my_strcmp(_initrd_files[i].name, filename) == 0) {
            if (offset + size > _initrd_files[i].size) return -2;
            /* Read via Multiboot mod_start (identity-mapped low RAM). The initrd
             * VA window (0x05000000+) lives only in kernel_page_table; boot_pml4
             * does not map it, so f->data would #PF before CR3 switch. */
            const unsigned char *src =
                (const unsigned char *)(uintptr_t)(_initrd_files[i].phys_base + offset);
            unsigned char *dst = (unsigned char*)buf;
            for (uint64_t j = 0; j < size; ++j) dst[j] = src[j];
            return 0;
        }
    }

    return -3; // not found
}

int bfree_initrd_read(const char *filename, uint64_t offset, void *buf, uint64_t size)
{
    return read_file(filename, offset, buf, size);
}

/* Below initrd VA window (0x05000000). Large desktop.elf PT_LOAD uses pmm_alloc()
 * above the initrd reservation (~100 MiB low RAM). Only embedded kernel PT slots
 * (PT_EMBEDDED_COUNT*2MiB) are guaranteed identity-mapped; higher phys use staging. */
#define BFREE_LOADER_STAGING_VA 0x04E00000ULL
static uint8_t g_elf_loader_page_buf[4096];
static uint64_t g_loader_staging_phys;

static int bfree_loader_write_phys(uint64_t phys, const void *src, uint64_t len);
static int bfree_loader_zero_phys(uint64_t phys, uint64_t len);
static int bfree_loader_read_phys(uint64_t phys, void *dst, uint64_t len);

/* Always stage — identity VA==PA stores under kernel CR3 have disagreed with
 * later task-PT walks of the same phys (child ELF pages stayed zero; ring3
 * executed 00 00 = add %al,(%rax) then #PF/#GP). */
static int bfree_loader_poke_phys(uint64_t phys, const void *src, uint64_t len)
{
    return bfree_loader_write_phys(phys, src, len);
}

static int bfree_loader_clear_phys(uint64_t phys, uint64_t len)
{
    return bfree_loader_zero_phys(phys, len);
}

static int bfree_loader_stage_phys(uint64_t phys)
{
    uint64_t base = phys & ~(PAGE_SIZE - 1ULL);
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t pte;

    pd_index = (BFREE_LOADER_STAGING_VA >> 21) & 0x1FFULL;
    pt_index = (BFREE_LOADER_STAGING_VA >> 12) & 0x1FFULL;
    pte = kernel_page_table.pt[pd_index][pt_index];
    /* Reuse only when the PTE still targets this frame. phys_io_end used to
     * release staging while a nested begin was live, leaving g_loader_staging_phys
     * stale so subsequent writes hit the identity staging frame. */
    if (g_loader_staging_phys == base &&
        (pte & 0x001ULL) != 0ULL &&
        (pte & ~(PAGE_SIZE - 1ULL)) == base) {
        return 0;
    }
    if (vmm_map_page(&kernel_page_table, BFREE_LOADER_STAGING_VA, base, 0x003ULL) != 0) {
        return -1;
    }
    __asm__ volatile("invlpg (%0)" : : "r"(BFREE_LOADER_STAGING_VA) : "memory");
    g_loader_staging_phys = base;
    return 0;
}

static void bfree_loader_release_staging(void)
{
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t id_phys;

    if (!g_loader_staging_phys) {
        return;
    }
    pd_index = (BFREE_LOADER_STAGING_VA >> 21) & 0x1FFULL;
    pt_index = (BFREE_LOADER_STAGING_VA >> 12) & 0x1FFULL;
    id_phys = ((pd_index * (uint64_t)PTE_COUNT) + pt_index) * PAGE_SIZE;
    kernel_page_table.pt[pd_index][pt_index] = id_phys | 0x003ULL;
    __asm__ volatile("invlpg (%0)" : : "r"(BFREE_LOADER_STAGING_VA) : "memory");
    g_loader_staging_phys = 0;
}

static int bfree_loader_write_phys(uint64_t phys, const void *src, uint64_t len)
{
    uint64_t off = phys & (PAGE_SIZE - 1ULL);
    const uint8_t *s = (const uint8_t *)src;
    volatile uint8_t *dst;
    uint64_t rflags;
    uint64_t i;

    if (len == 0 || off + len > PAGE_SIZE) {
        return -1;
    }
    {
        uint64_t base = phys & ~(PAGE_SIZE - 1ULL);
        if ((g_bfree_elf_watch_phys != 0 && base == g_bfree_elf_watch_phys) ||
            (g_bfree_elf_watch_phys2 != 0 && base == g_bfree_elf_watch_phys2) ||
            (g_bfree_shell_text_phys != 0 && base == g_bfree_shell_text_phys) ||
            bfree_shell_page_pinned(base)) {
            uart_puts("[ELF] FATAL: write_phys hits watch phys=");
            uart_puthex64(base);
            uart_puts("\n");
            return -1;
        }
    }
    /* Keep IRQs from running phys_io_end / release_staging mid-write. */
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags) : : "memory");
    if (bfree_loader_stage_phys(phys) != 0) {
        if (rflags & 0x200ULL) {
            __asm__ volatile("sti" ::: "memory");
        }
        return -1;
    }
    dst = (volatile uint8_t *)(uintptr_t)(BFREE_LOADER_STAGING_VA + off);
    for (i = 0; i < len; ++i) {
        dst[i] = s[i];
    }
    /* Readback: catch a stale staging PTE before the guest executes zeros. */
    for (i = 0; i < len; ++i) {
        if (dst[i] != s[i]) {
            if (rflags & 0x200ULL) {
                __asm__ volatile("sti" ::: "memory");
            }
            return -1;
        }
    }
    if (rflags & 0x200ULL) {
        __asm__ volatile("sti" ::: "memory");
    }
    return 0;
}

static int bfree_loader_zero_phys(uint64_t phys, uint64_t len)
{
    uint64_t off = phys & (PAGE_SIZE - 1ULL);
    volatile uint8_t *dst;
    uint64_t rflags;
    uint64_t i;
    uint64_t base = phys & ~(PAGE_SIZE - 1ULL);

    if (len == 0 || off + len > PAGE_SIZE) {
        return -1;
    }
    if ((g_bfree_elf_watch_phys != 0 && base == g_bfree_elf_watch_phys) ||
        (g_bfree_elf_watch_phys2 != 0 && base == g_bfree_elf_watch_phys2) ||
        (g_bfree_shell_text_phys != 0 && base == g_bfree_shell_text_phys) ||
        bfree_shell_page_pinned(base)) {
        uart_puts("[ELF] FATAL: zero_phys hits watch phys=");
        uart_puthex64(base);
        uart_puts("\n");
        return -1;
    }
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags) : : "memory");
    if (bfree_loader_stage_phys(phys) != 0) {
        if (rflags & 0x200ULL) {
            __asm__ volatile("sti" ::: "memory");
        }
        return -1;
    }
    dst = (volatile uint8_t *)(uintptr_t)(BFREE_LOADER_STAGING_VA + off);
    for (i = 0; i < len; ++i) {
        dst[i] = 0;
    }
    if (rflags & 0x200ULL) {
        __asm__ volatile("sti" ::: "memory");
    }
    return 0;
}

static int load_elf_image_inner(const char *filename, void **entry, void *page_table_base)
{
    elf64_ehdr_t ehdr;
    int res = -1;

    g_last_loaded_elf.valid = 0;
    g_bfree_elf_watch_phys = 0;
    /* Keep sticky shell .text protection across vfork+exec child loads. */
    g_bfree_elf_watch_phys2 = g_bfree_shell_text_phys;

    if (read_file(filename, 0, &ehdr, sizeof(ehdr)) < 0) {
        res = -1;
        goto out;
    }
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' || ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        res = -2;
        goto out;
    }
#if BFREE_BOOT_DEBUG
    uart_puts("[ELF] entry=");
    uart_puthex64(ehdr.e_entry);
    uart_puts(" phoff=");
    uart_puthex64(ehdr.e_phoff);
    uart_puts(" phentsize=");
    uart_puthex64(ehdr.e_phentsize);
    uart_puts(" phnum=");
    uart_puthex64(ehdr.e_phnum);
    uart_puts("\n");
#endif
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        elf64_phdr_t phdr;
        uint64_t phoff = ehdr.e_phoff + i * ehdr.e_phentsize;
        if (read_file(filename, phoff, &phdr, sizeof(phdr)) < 0) {
            res = -3;
            goto out;
        }
#if BFREE_BOOT_DEBUG
        uart_puts("[ELF] ph[");
        uart_puthex64(i);
        uart_puts("] type=");
        uart_puthex64(phdr.p_type);
        uart_puts(" vaddr=");
        uart_puthex64(phdr.p_vaddr);
        uart_puts(" offset=");
        uart_puthex64(phdr.p_offset);
        uart_puts(" filesz=");
        uart_puthex64(phdr.p_filesz);
        uart_puts(" memsz=");
        uart_puthex64(phdr.p_memsz);
        uart_puts("\n");
    #endif
        if (phdr.p_type != 1) continue; // PT_LOADのみ
        if (phdr.p_vaddr + phdr.p_memsz > VMM_USER_VA_BYTES) {
            uart_puts("[ELF] segment above user VA limit vaddr=");
            uart_puthex64(phdr.p_vaddr);
            uart_puts(" memsz=");
            uart_puthex64(phdr.p_memsz);
            uart_puts("\n");
            res = -7;
            goto out;
        }
        /* Map every page touched by [p_vaddr, p_vaddr+p_memsz), honoring p_vaddr
         * misalignment (musl/busybox .data often starts mid-page). */
        uint64_t seg_start = phdr.p_vaddr;
        uint64_t seg_filesz = phdr.p_filesz;
        uint64_t seg_memsz = phdr.p_memsz;
        uint64_t seg_file_end = seg_start + seg_filesz;
        uint64_t seg_mem_end = seg_start + seg_memsz;
        uint64_t page_start = seg_start & ~(PAGE_SIZE - 1ULL);
        uint64_t page_end = (seg_mem_end + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
        uint64_t offset = phdr.p_offset;
        uint64_t page_count = 0;
        uint64_t pte_flags = 0x005ULL;
        if (phdr.p_flags & 0x2u) {
            pte_flags = 0x007ULL;
        } else if (phdr.p_flags & 0x4u) {
            pte_flags = 0x005ULL;
        }
        for (uint64_t va = page_start; va < page_end; va += PAGE_SIZE) {
#if BFREE_BOOT_DEBUG
            uart_puts("[ELF] map vaddr=");
            uart_puthex64(va);
            uart_puts("\n");
#endif
            if ((page_count & 0x1FFULL) == 0ULL &&
                (page_end - page_start) > (512ULL * PAGE_SIZE)) {
                uart_puts("[ELF] loading pages=");
                uart_puthex64((va - page_start) >> 12);
                uart_puts("/");
                uart_puthex64((page_end - page_start) >> 12);
                uart_puts("\n");
            }
            page_count++;
            void *page = pmm_alloc();
            if (!page) {
                res = -4;
                goto out;
            }
            /* Map Writable for fill: with CR0.WP=1, supervisor stores into a
             * User|RO PTE fault. Final RO/RX flags are applied after populate. */
            if (vmm_map_page(page_table_base, va, (uint64_t)page,
                             (pte_flags | 0x002ULL)) != 0) {
                res = -5;
                goto out;
            }
            /* Drop VA==phys identity on destination AND kernel_page_table so
             * ring0 identity stores cannot clobber this frame. */
            vmm_drop_identity_alias((page_table_t *)page_table_base,
                                    (uint64_t)(uintptr_t)page);
            vmm_drop_identity_alias(&kernel_page_table,
                                    (uint64_t)(uintptr_t)page);
            /*
             * Populate via staging under kernel CR3 into the PTE's frame.
             * CR3-VA fills can disagree with later staging peeks of the same
             * phys for some pages (shell .text @0x522ea0 was 06 00.. on AS-copy
             * while fopen @0x521fa0 stayed good). Staging write+readback is the
             * same path guest phys walks use after identity is dropped.
             */
            {
                uint64_t copy_start = va;
                uint64_t copy_end = va + PAGE_SIZE;
                uint64_t page_off = 0;
                uint64_t to_copy = 0;
                int have_file = 0;
                int did_verify = 0;
                uint8_t verify_b0 = 0, verify_b1 = 0, verify_b2 = 0, verify_b3 = 0;
                uint64_t phys = (uint64_t)(uintptr_t)page;

                if (copy_start < seg_start) {
                    copy_start = seg_start;
                }
                if (copy_end > seg_file_end) {
                    copy_end = seg_file_end;
                }
                if (copy_start < copy_end) {
                    uint64_t file_off = offset + (copy_start - seg_start);
                    page_off = copy_start - va;
                    to_copy = copy_end - copy_start;
                    if (read_file(filename, file_off, g_elf_loader_page_buf, to_copy) < 0) {
                        res = -6;
                        goto out;
                    }
                    have_file = 1;
                }

                bfree_kernel_phys_io_begin();
                if (bfree_loader_clear_phys(phys, PAGE_SIZE) != 0) {
                    bfree_kernel_phys_io_end();
                    res = -6;
                    goto out;
                }
                if (have_file) {
                    if (bfree_loader_poke_phys(phys + page_off, g_elf_loader_page_buf,
                                               to_copy) != 0) {
                        bfree_kernel_phys_io_end();
                        res = -6;
                        goto out;
                    }
                    if (va == 0x521000ULL && page_off == 0ULL && to_copy >= 0xFB0ULL) {
                        uint8_t chk[4];
                        if (bfree_loader_read_phys(phys + 0xFA0ULL, chk, 4ULL) == 0) {
                            verify_b0 = chk[0];
                            verify_b1 = chk[1];
                            verify_b2 = chk[2];
                            verify_b3 = chk[3];
                            did_verify = 1;
                        }
                    }
                    if (va == 0x522000ULL && page_off == 0ULL && to_copy >= 0xEA4ULL) {
                        uint8_t chk[4];
                        if (bfree_loader_read_phys(phys + 0xEA0ULL, chk, 4ULL) == 0) {
                            verify_b0 = chk[0];
                            verify_b1 = chk[1];
                            verify_b2 = chk[2];
                            verify_b3 = chk[3];
                            did_verify = 2;
                        }
                    }
                }
                bfree_kernel_phys_io_end();

                /* Restore final PTE flags (drop temporary Writable on RO text). */
                if ((pte_flags & 0x002ULL) == 0ULL) {
                    if (vmm_map_page(page_table_base, va, (uint64_t)page, pte_flags) != 0) {
                        res = -5;
                        goto out;
                    }
                }
                /*
                 * Pin RO/RX frames of the first BusyBox image as they are filled.
                 * After poke so fill itself is never refused; pin_done latches once
                 * the sticky setvbuf frame has been recorded for this image.
                 */
                if (!g_bfree_shell_pin_done && (pte_flags & 0x002ULL) == 0ULL &&
                    va >= 0x500000ULL && va < 0x580000ULL &&
                    filename && filename[0] == 'b' /* busybox.elf */) {
                    bfree_shell_pin_page(phys);
                }
                if (did_verify) {
                    uart_puts(did_verify == 2
                                  ? "[ELF] verify 0x522ea0 bytes="
                                  : "[ELF] verify 0x521fa0 bytes=");
                    uart_puthex64((uint64_t)verify_b0);
                    uart_puts(" ");
                    uart_puthex64((uint64_t)verify_b1);
                    uart_puts(" ");
                    uart_puthex64((uint64_t)verify_b2);
                    uart_puts(" ");
                    uart_puthex64((uint64_t)verify_b3);
                    uart_puts(" phys=");
                    uart_puthex64(phys);
                    uart_puts("\n");
                    if (verify_b0 != 0xf3u || verify_b1 != 0x0fu ||
                        verify_b2 != 0x1eu || verify_b3 != 0xfau) {
                        /* Soft: endbr64 fingerprint moves when BusyBox is rebuilt (F). */
                        uart_puts(did_verify == 2
                                      ? "[ELF] WARN: 0x522ea0 fingerprint moved\n"
                                      : "[ELF] WARN: 0x521fa0 fingerprint moved\n");
                    }
                    if (did_verify == 1) {
                        g_bfree_elf_watch_phys = phys;
                    } else if (did_verify == 2) {
                        if (g_bfree_shell_text_phys == 0) {
                            g_bfree_shell_text_phys = phys;
                            uart_puts("[ELF] shell text watch phys=");
                            uart_puthex64(phys);
                            uart_puts("\n");
                        }
                        g_bfree_elf_watch_phys2 = g_bfree_shell_text_phys;
                    }
                }
            }
        }
        uart_puts("[ELF] PT_LOAD vaddr=");
        uart_puthex64(phdr.p_vaddr);
        uart_puts(" pages=");
        uart_puthex64((page_end - page_start) >> 12);
        uart_puts("\n");
    }
    if (entry) *entry = (void*)(uintptr_t)ehdr.e_entry;
    g_last_loaded_elf.entry = ehdr.e_entry;
    g_last_loaded_elf.phdr_vaddr = ehdr.e_phoff;
    g_last_loaded_elf.phnum = ehdr.e_phnum;
    g_last_loaded_elf.phentsize = ehdr.e_phentsize;
    for (int pi = 0; pi < ehdr.e_phnum; ++pi) {
        elf64_phdr_t phdr_scan;
        uint64_t phoff_scan = ehdr.e_phoff + (uint64_t)pi * ehdr.e_phentsize;
        if (read_file(filename, phoff_scan, &phdr_scan, sizeof(phdr_scan)) < 0) {
            break;
        }
        if (phdr_scan.p_type != 1u) {
            continue;
        }
        if (ehdr.e_phoff >= phdr_scan.p_offset &&
            ehdr.e_phoff < phdr_scan.p_offset + phdr_scan.p_filesz) {
            g_last_loaded_elf.phdr_vaddr =
                phdr_scan.p_vaddr + (ehdr.e_phoff - phdr_scan.p_offset);
            break;
        }
    }
    g_last_loaded_elf.valid = 1;
    res = 0;

    /* Re-check wc-fault page after ALL segments — catches later fills
     * clobbering phys already installed for .text. */
    if (page_table_base) {
        uint64_t pd_index = (0x521000ULL >> 21) & 0x1FFULL;
        uint64_t pt_index = (0x521000ULL >> 12) & 0x1FFULL;
        page_table_t *pt = (page_table_t *)page_table_base;
        uint64_t pte = 0;
        if (pd_index < PT_EMBEDDED_COUNT) {
            pte = pt->pt[pd_index][pt_index];
        }
        if ((pte & 0x001ULL) != 0ULL) {
            uint64_t phys = pte & ~(PAGE_SIZE - 1ULL);
            uint8_t chk[4];
            bfree_kernel_phys_io_begin();
            if (bfree_kernel_peek_phys(phys + 0xFA0ULL, chk, 4ULL) == 0) {
                uart_puts("[ELF] post-load 0x521fa0 bytes=");
                uart_puthex64((uint64_t)chk[0]);
                uart_puts(" ");
                uart_puthex64((uint64_t)chk[1]);
                uart_puts(" ");
                uart_puthex64((uint64_t)chk[2]);
                uart_puts(" ");
                uart_puthex64((uint64_t)chk[3]);
                uart_puts(" phys=");
                uart_puthex64(phys);
                uart_puts("\n");
            }
            bfree_kernel_phys_io_end();
        }
        pd_index = (0x522000ULL >> 21) & 0x1FFULL;
        pt_index = (0x522000ULL >> 12) & 0x1FFULL;
        pte = 0;
        if (pd_index < PT_EMBEDDED_COUNT) {
            pte = pt->pt[pd_index][pt_index];
        }
        if ((pte & 0x001ULL) != 0ULL) {
            uint64_t phys2 = pte & ~(PAGE_SIZE - 1ULL);
            uint8_t chk2[4];
            bfree_kernel_phys_io_begin();
            if (bfree_kernel_peek_phys(phys2 + 0xEA0ULL, chk2, 4ULL) == 0) {
                uart_puts("[ELF] post-load 0x522ea0 bytes=");
                uart_puthex64((uint64_t)chk2[0]);
                uart_puts(" ");
                uart_puthex64((uint64_t)chk2[1]);
                uart_puts(" ");
                uart_puthex64((uint64_t)chk2[2]);
                uart_puts(" ");
                uart_puthex64((uint64_t)chk2[3]);
                uart_puts(" phys=");
                uart_puthex64(phys2);
                uart_puts("\n");
            }
            bfree_kernel_phys_io_end();
        }
    }

    if (res == 0 && !g_bfree_shell_pin_done && g_bfree_shell_text_phys != 0 &&
        g_bfree_shell_pin_count > 0) {
        g_bfree_shell_pin_done = 1;
        uart_puts("[ELF] shell pinned pages=");
        uart_puthex64((uint64_t)(unsigned)g_bfree_shell_pin_count);
        uart_puts("\n");
    }

out:
    bfree_loader_release_staging();
    return res;
}

/* Ring3 user stack used by syscall/exec_initrd (see main.c USER_STACK_TOP). */
#define BFREE_USER_STACK_LO 0x00200000ULL
#define BFREE_USER_STACK_HI 0x01400000ULL

/* Dedicated stack for syscall-time ELF load (desktop.elf ~34 MiB, deep call chain).
 * Do NOT reuse knl_kernel_stack_top-128: only 128 bytes remain -> stack overflow -> #GP in my_strcmp. */
#define BFREE_ELF_LOADER_STACK_BYTES 16384U
static uint8_t g_elf_loader_stack[BFREE_ELF_LOADER_STACK_BYTES] __attribute__((aligned(16)));

/* Syscall loader: spill filename/entry/page_table to .bss BEFORE RSP switch.
 * Otherwise the compiler keeps `filename` on the ring3 stack; after CR3->kernel_page_table
 * my_strcmp dereferences a stale VA -> #PF/#GP (CR2 looks like garbage). */
static char g_elf_loader_filename[64];
static void *g_elf_loader_entry_out;
static void *g_elf_loader_page_table;
static void **g_elf_loader_entry_dest;
static uintptr_t g_elf_loader_saved_rsp;
static uintptr_t g_elf_loader_saved_cr3;
static int g_elf_loader_use_alt_stack;

static void bfree_elf_loader_copy_name(const char *filename)
{
    size_t i = 0;

    if (!filename) {
        g_elf_loader_filename[0] = '\0';
        return;
    }
    while (filename[i] && i + 1 < sizeof(g_elf_loader_filename)) {
        g_elf_loader_filename[i] = filename[i];
        ++i;
    }
    g_elf_loader_filename[i] = '\0';
}

int load_elf_image(const char *filename, void **entry, void *page_table_base) {
    uintptr_t saved_rsp;
    uintptr_t ksp;
    int use_loader_stack = 0;
    /* Locals must not live on the temporary loader stack — RSP is restored before return. */
    static volatile int load_result;

    bfree_elf_loader_copy_name(filename);
    g_elf_loader_entry_out = 0;
    g_elf_loader_page_table = page_table_base;
    g_elf_loader_entry_dest = entry;

    __asm__ volatile("mov %%rsp, %0" : "=r"(saved_rsp) : : "memory");
    g_elf_loader_saved_rsp = saved_rsp;
    __asm__ volatile("mov %%cr3, %0" : "=r"(g_elf_loader_saved_cr3) : : "memory");

    /* SYSCALL entry keeps the ring3 RSP. Switching CR3 to kernel_page_table without
     * switching RSP maps the user stack VA to the wrong physical pages -> #GP.
     * knl_main already runs on knl_kernel_stack_top — switching RSP there corrupts
     * the boot frame and can hang before init.elf returns to userland. */
    if (saved_rsp >= BFREE_USER_STACK_LO && saved_rsp < BFREE_USER_STACK_HI) {
        use_loader_stack = 1;
        ksp = (uintptr_t)&g_elf_loader_stack[BFREE_ELF_LOADER_STACK_BYTES];
        ksp &= ~(uintptr_t)0xFULL;
        ksp -= 128;
        __asm__ volatile("mov %0, %%rsp" :: "r"(ksp) : "memory");
    }
    g_elf_loader_use_alt_stack = use_loader_stack;

    /* PMM identity VA; loader must use kernel_page_table (see load_elf_image_inner). */
    vmm_activate_kernel_page_table();
    load_result = load_elf_image_inner(g_elf_loader_filename, &g_elf_loader_entry_out,
                                       g_elf_loader_page_table);

    /* Log while still on kernel_page_table (safe for boot + syscall loader stacks). */
    uart_puts("[ELF] load ");
    uart_puts(g_elf_loader_filename);
    uart_puts(" code=");
    uart_puthex64((uint64_t)(int64_t)load_result);
    uart_puts(" entry=");
    uart_puthex64((uint64_t)(uintptr_t)g_elf_loader_entry_out);
    uart_puts("\n");

    /*
     * Restore the caller's CR3 (not page_table_base). Activating a private child
     * PT here while RSP still points at the parent's user stack #PF (CR2=0).
     * CR3 for the new image is switched atomically with the new RSP in
     * syscall_entry.S (g_bfree_sysret_exec_cr3).
     * Order: CR3 first (while still on loader stack), then restore user RSP.
     */
    __asm__ volatile("mov %0, %%cr3" :: "r"(g_elf_loader_saved_cr3) : "memory");
    if (g_elf_loader_use_alt_stack) {
        __asm__ volatile("mov %0, %%rsp" :: "r"(g_elf_loader_saved_rsp) : "memory");
    }
    if (g_elf_loader_entry_dest) {
        *g_elf_loader_entry_dest = g_elf_loader_entry_out;
    }
    return load_result;
}

void *pmm_alloc(void);

static uintptr_t g_kernel_phys_io_saved_cr3;
static int g_kernel_phys_io_depth;

void bfree_kernel_phys_io_begin(void)
{
    if (g_kernel_phys_io_depth == 0) {
        __asm__ volatile("mov %%cr3, %0" : "=r"(g_kernel_phys_io_saved_cr3) : : "memory");
        vmm_activate_kernel_page_table();
    }
    g_kernel_phys_io_depth++;
}

void bfree_kernel_phys_io_end(void)
{
    if (g_kernel_phys_io_depth <= 0) {
        return;
    }
    g_kernel_phys_io_depth--;
    /* Only tear down staging when the outermost phys_io scope ends. Nested
     * end() used to release STAGING_VA while the outer scope still expected
     * g_loader_staging_phys → silent writes into the wrong frame. */
    if (g_kernel_phys_io_depth == 0) {
        bfree_loader_release_staging();
        __asm__ volatile("mov %0, %%cr3" :: "r"(g_kernel_phys_io_saved_cr3) : "memory");
    }
}

int bfree_kernel_clear_phys(uint64_t phys, uint64_t len)
{
    int rc;
    bfree_kernel_phys_io_begin();
    rc = bfree_loader_clear_phys(phys, len);
    bfree_kernel_phys_io_end();
    return rc;
}

int bfree_kernel_poke_phys(uint64_t phys, const void *src, uint64_t len)
{
    int rc;
    bfree_kernel_phys_io_begin();
    rc = bfree_loader_poke_phys(phys, src, len);
    bfree_kernel_phys_io_end();
    return rc;
}

int bfree_kernel_poke_phys_staged(uint64_t phys, const void *src, uint64_t len)
{
    int rc;
    bfree_kernel_phys_io_begin();
    rc = bfree_loader_write_phys(phys, src, len);
    bfree_kernel_phys_io_end();
    return rc;
}

static int bfree_loader_read_phys(uint64_t phys, void *dst, uint64_t len)
{
    uint64_t off = phys & (PAGE_SIZE - 1ULL);
    const uint8_t *src;
    uint8_t *d = (uint8_t *)dst;

    if (len == 0 || off + len > PAGE_SIZE) {
        return -1;
    }
    if (bfree_loader_stage_phys(phys) != 0) {
        return -1;
    }
    src = (const uint8_t *)(uintptr_t)(BFREE_LOADER_STAGING_VA + off);
    for (uint64_t i = 0; i < len; ++i) {
        d[i] = src[i];
    }
    return 0;
}

int bfree_kernel_peek_phys(uint64_t phys, void *dst, uint64_t len)
{
    int rc;

    /* Staging updates kernel_page_table only; without kernel CR3, a child
     * CR3 walk hits the identity STAGING_VA hole and returns false zeros. */
    bfree_kernel_phys_io_begin();
    rc = bfree_loader_read_phys(phys, dst, len);
    bfree_kernel_phys_io_end();
    return rc;
}

void bfree_kernel_zero_phys_page(uint64_t phys)
{
    bfree_kernel_phys_io_begin();
    (void)bfree_loader_clear_phys(phys, PAGE_SIZE);
    bfree_kernel_phys_io_end();
}

// --- string.h不要の簡易strcmp ---
static int my_strcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return *a - *b;
}
