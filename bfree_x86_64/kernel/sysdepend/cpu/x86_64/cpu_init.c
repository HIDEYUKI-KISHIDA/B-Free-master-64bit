#include <stdint.h>
#include <stddef.h>
#include "tk/fpu_state.h"

extern int g_fpu_save_mode; // 0=FXSAVE, 1=XSAVE, 2=XSAVEOPT
extern uint64_t g_fpu_rfbm; // 保存マスク（x87+SSE+AVX+AVX512）

// syscall_entryはアセンブリで定義されているためextern宣言
extern void syscall_entry(void);
extern void bfree_enable_user_fpu(void);


typedef struct {
    int has_sse;
    int has_avx;
    int has_xsave;
    int has_xsaveopt;
    int has_avx512;
    uint32_t xsave_features; // XCR0
    uint32_t xsave_size;
} cpu_feature_info_t;
cpu_feature_info_t cpu_features;

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

static inline void cpuid_ex(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
}

static inline uint64_t xgetbv(uint32_t index) {
    uint32_t eax, edx;
    __asm__ volatile ("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}

void detect_cpu_features(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    cpu_features.has_sse = (edx & (1 << 25)) && (edx & (1 << 26));
    cpu_features.has_avx = (ecx & (1 << 28));
    cpu_features.has_xsave = (ecx & (1 << 26));

    if (cpu_features.has_xsave) {
        uint64_t xcr0 = xgetbv(0);
        cpu_features.xsave_features = (uint32_t)xcr0;
        // AVXサポートはXCR0[2:1]=11b
        if ((xcr0 & 0x6) == 0x6) cpu_features.has_avx = 1;
        // AVX512サポートはXCR0[7]=1
        cpu_features.has_avx512 = (xcr0 & (1 << 7)) ? 1 : 0;
        // XSAVEOPTサポートはCPUID(0xD,1):EAX[0]=1
        cpuid_ex(0xD, 1, &eax, &ebx, &ecx, &edx);
        cpu_features.has_xsaveopt = (eax & 1) ? 1 : 0;
        // XSAVE領域サイズ
        cpuid_ex(0xD, 0, &eax, &ebx, &ecx, &edx);
        cpu_features.xsave_size = ecx;
    } else {
        cpu_features.xsave_features = 0;
        cpu_features.has_avx512 = 0;
        cpu_features.has_xsaveopt = 0;
        cpu_features.xsave_size = 512; // FXSAVE
    }


    // printf("[CPU] SSE=%d AVX=%d XSAVE=%d XSAVEOPT=%d AVX512=%d XSAVE_SIZE=%u\n",
    //     cpu_features.has_sse, cpu_features.has_avx, cpu_features.has_xsave,
    //     cpu_features.has_xsaveopt, cpu_features.has_avx512, cpu_features.xsave_size);

    // XSAVE/XSAVEOPT/FXSAVE自動切替
    if (cpu_features.has_xsave) {
        if (cpu_features.has_xsaveopt) {
            g_fpu_save_mode = 2; // XSAVEOPT
        } else {
            g_fpu_save_mode = 1; // XSAVE
        }
    } else {
        g_fpu_save_mode = 0; // FXSAVE
    }
    // rfbm: x87+SSE+AVX+AVX512
    g_fpu_rfbm = 0x7; // x87+SSE+AVX
    if (cpu_features.has_avx512) g_fpu_rfbm |= (1ULL << 7); // AVX512

    // printf("[FPU] g_fpu_save_mode=%d rfbm=0x%llx\n", g_fpu_save_mode, g_fpu_rfbm);
}
/*
 * cpu_init.c - x86_64 T-Kernel2.0 CPU初期化
 * 仕様: TK2_x86_64_Spec.md v2.3 2.5節準拠
 */
#include <stdint.h>

/* TSS構造体（64bit） */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss64_t;

/* 外部シンボル: スタックトップ */
extern uint8_t knl_kernel_stack_top[];

/* TSS実体 */
static tss64_t tss __attribute__((aligned(16)));

/* GDTエントリ（16バイトアライン） */
__attribute__((aligned(16)))
static uint64_t gdt[8];

/* GDT初期化 */
static void setup_gdt(void) {
    // GDTレイアウト:
    // [0]=null [1]=ring0 code(0x08) [2]=ring0 data(0x10)
    // [3]=null/pad(0x18)  ← sysretqのSTAR[63:48]=0x1B用の基点
    // [4]=user data DPL=3(0x20, RPL=3 → 0x23)  ← sysretqがSSをここにセット
    // [5]=user code DPL=3(0x28, RPL=3 → 0x2B)  ← sysretqがCSをここにセット
    // [6+7]=TSS(selector=0x30)
    // sysretq: CS=STAR[63:48]+16=0x1B+16=0x2B, SS=STAR[63:48]+8=0x1B+8=0x23
    gdt[0] = 0x0000000000000000ULL; // Null
    gdt[1] = 0x00af9a000000ffffULL; // ring0 64bit Code (L=1, D=0)
    gdt[2] = 0x00af92000000ffffULL; // ring0 Data
    gdt[3] = 0x0000000000000000ULL; // null/padding (0x18) ← STAR base
    gdt[4] = 0x00aff2000000ffffULL; // User Data DPL=3 (0x20, RPL=3→0x23)
    gdt[5] = 0x00affa000000ffffULL; // User 64bit Code DPL=3 (0x28, RPL=3→0x2B)
    // TSS Descriptor at GDT[6+7] (selector=0x30)
    uint64_t base = (uint64_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    gdt[6] = (limit & 0xFFFFULL) |
             ((base & 0xFFFFFFULL) << 16) |
             (0x89ULL << 40) | // type=0x9(TSS Available), S=0, DPL=0, P=1
             ((limit & 0xF0000ULL) << 32) |
             ((base & 0xFF000000ULL) << 32);
    gdt[7] = base >> 32;
}

/* GDTディスクリプタ */
struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_desc;

void knl_cpu_init(void) {
        detect_cpu_features();
    // TSS初期化
    for (size_t i = 0; i < sizeof(tss); ++i) ((uint8_t*)&tss)[i] = 0;
    tss.rsp0 = (uint64_t)knl_kernel_stack_top;
    tss.iomap_base = sizeof(tss);

    // GDT初期化
    setup_gdt();
    gdt_desc.limit = sizeof(gdt) - 1;
    gdt_desc.base = (uint64_t)gdt;

    // GDTロード
    __asm__ volatile ("lgdt %0" : : "m"(gdt_desc));
    // TSSロード（セレクタ0x30 = GDT[6]）
    __asm__ volatile ("ltr %%ax" : : "a"(0x30));

    // SYSCALL/SYSRET用MSR初期化（雛形）
    // IA32_STAR:
    // [47:32] kernel CS for SYSCALL entry
    // [63:48] base selector used by SYSRET (CS=base+0x10, SS=base+0x18)
    // STAR[63:48]=0x1B: sysretq→CS=0x2B(GDT[5]ring3 code), SS=0x23(GDT[4]ring3 data)
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x1B << 48);
    uint64_t lstar = (uint64_t)&syscall_entry;
    uint64_t sfmask = 0x200; // Clear IF on syscall
    __asm__ volatile ("wrmsr" :: "c"(0xC0000081), "a"((uint32_t)star), "d"((uint32_t)(star >> 32)));
    __asm__ volatile ("wrmsr" :: "c"(0xC0000082), "a"((uint32_t)lstar), "d"((uint32_t)(lstar >> 32)));
    __asm__ volatile ("wrmsr" :: "c"(0xC0000084), "a"((uint32_t)sfmask), "d"(0));
    // EFER.SCE (bit0)有効化
    uint32_t eax, edx;
    __asm__ volatile ("rdmsr" : "=a"(eax), "=d"(edx) : "c"(0xC0000080));
    eax |= 1;
    __asm__ volatile ("wrmsr" :: "c"(0xC0000080), "a"(eax), "d"(edx));
    bfree_enable_user_fpu();
}
