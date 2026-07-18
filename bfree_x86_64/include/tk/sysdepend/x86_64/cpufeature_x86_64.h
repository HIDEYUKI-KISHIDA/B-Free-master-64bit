#ifndef __TK_CPUFEATURE_X86_64__
#define __TK_CPUFEATURE_X86_64__

/*
 * x86_64用 CPU機能検出・バリデーション・安全分岐サンプル
 * Linuxカーネル流の堅牢性・拡張性考慮
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CPU機能情報構造体 */
typedef struct {
    uint32_t vendor;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    int has_sse;
    int has_avx;
    int has_smep;
    int has_smap;
    int has_nx;
    int has_xsave;
    int has_numa;
    int has_smp;
    int is_blacklisted; /* 既知バグ/制限用 */
} cpu_feature_info_t;

/* 機能検出・バリデーションAPI */
void detect_cpu_features_x86_64(cpu_feature_info_t *info);
int validate_acpi_table(const void *table, uint32_t len);
void apply_cpu_blacklist_workarounds(cpu_feature_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* __TK_CPUFEATURE_X86_64__ */
