/*
 * STOS BSP — Sony Xperia 10 III (lena / PDX213 / SM6350)
 *
 * Pattern A (spec v0.5.1 §8.1): DTS is the hardware source of truth.
 * Values below are transcribed from mainline Linux:
 *   arch/arm64/boot/dts/qcom/sm6350.dtsi
 *   arch/arm64/boot/dts/qcom/sm6350-sony-xperia-lena-pdx213.dts
 *
 * DRAM size is NOT in DTS (bootloader fills memory@80000000 size=0).
 * Default is the 6 GiB retail SKU; confirm on device with:
 *   grep -iE 'System RAM|reserved' /proc/iomem
 *
 * Phase 1 exit: every STOS_* symbol required by §8.1 is defined.
 */

#ifndef STOS_BSP_LENA_PDX213_H
#define STOS_BSP_LENA_PDX213_H

/* --- §8.1 required symbols --- */

#define STOS_DRAM_BASE            0x80000000ULL
#define STOS_DRAM_SIZE            0x180000000ULL /* 6 GiB typical SKU; confirm /proc/iomem */
#define STOS_KERNEL_LOAD_BASE     0x88000000ULL /* spec §7 / §16 kexec --mem-min */
#define STOS_GICD_BASE            0x17A00000ULL
#define STOS_GICR_STRIDE          0x20000ULL
#define STOS_GICR_BASE(cpu)       (0x17A60000ULL + (unsigned long long)(cpu) * STOS_GICR_STRIDE)
#define STOS_UART_DBG_BASE        0x0098C000ULL /* uart9 / qcom,geni-debug-uart */
#define STOS_TIMER_PNSIRQ         30U           /* GIC INTID: arch timer Physical NS (PPI 14) */
#define STOS_TIMER_CLK_HZ         19200000U
#define STOS_CPU_COUNT            8U

#define STOS_GICR_BASE_CPU0       0x17A60000ULL
#define STOS_UART_DBG_SIZE        0x4000ULL
#define STOS_GICD_SIZE            0x10000ULL
#define STOS_GICR_REGION_SIZE     0x100000ULL /* 8 redistributors */

/* --- Reserved DRAM (no-map). Do not identity-map or heap these. --- */

#define STOS_HYP_MEM_BASE         0x80000000ULL
#define STOS_HYP_MEM_SIZE         0x00600000ULL /* 0x80000000–0x805FFFFF; A-5: do not touch */

#define STOS_XBL_AOP_BASE         0x80700000ULL
#define STOS_XBL_AOP_SIZE         0x00160000ULL
#define STOS_CMD_DB_BASE          0x80860000ULL
#define STOS_CMD_DB_SIZE          0x00020000ULL
#define STOS_SEC_APPS_BASE        0x808FF000ULL
#define STOS_SEC_APPS_SIZE        0x00001000ULL
#define STOS_SMEM_BASE            0x80900000ULL
#define STOS_SMEM_SIZE            0x00200000ULL
#define STOS_CDSP_SEC_BASE        0x80B00000ULL
#define STOS_CDSP_SEC_SIZE        0x01E00000ULL
#define STOS_PIL_CAMERA_BASE      0x86000000ULL
#define STOS_PIL_CAMERA_SIZE      0x00500000ULL
#define STOS_PIL_NPU_BASE         0x86500000ULL
#define STOS_PIL_NPU_SIZE         0x00500000ULL
#define STOS_PIL_VIDEO_BASE       0x86A00000ULL
#define STOS_PIL_VIDEO_SIZE       0x00500000ULL
#define STOS_PIL_CDSP_BASE        0x86F00000ULL
#define STOS_PIL_CDSP_SIZE        0x01E00000ULL /* contains spec load addr 0x88000000 */
#define STOS_PIL_ADSP_BASE        0x88D00000ULL
#define STOS_PIL_ADSP_SIZE        0x02800000ULL
#define STOS_WLAN_FW_BASE         0x8B500000ULL
#define STOS_WLAN_FW_SIZE         0x00200000ULL
#define STOS_PIL_IPA_FW_BASE      0x8B700000ULL
#define STOS_PIL_IPA_FW_SIZE      0x00010000ULL
#define STOS_PIL_IPA_GSI_BASE     0x8B710000ULL
#define STOS_PIL_IPA_GSI_SIZE     0x00005400ULL
#define STOS_PIL_GPU_BASE         0x8B715400ULL
#define STOS_PIL_GPU_SIZE         0x00002000ULL
#define STOS_PIL_MODEM_BASE       0x8B800000ULL
#define STOS_PIL_MODEM_SIZE       0x0F800000ULL
#define STOS_CONT_SPLASH_BASE     0xA0000000ULL
#define STOS_CONT_SPLASH_SIZE     0x02300000ULL
#define STOS_DFPS_BASE            0xA2300000ULL
#define STOS_DFPS_SIZE            0x00100000ULL
#define STOS_REMOVED_REGION_BASE  0xC0000000ULL
#define STOS_REMOVED_REGION_SIZE  0x03900000ULL
#define STOS_DEBUG_REGION_BASE    0xFFB00000ULL
#define STOS_DEBUG_REGION_SIZE    0x000C0000ULL
#define STOS_LAST_LOG_BASE        0xFFBC0000ULL
#define STOS_LAST_LOG_SIZE        0x00040000ULL

/*
 * DTS-derived alternative load address: first 2 MiB-aligned DRAM after dfps
 * (0xA2400000). Spec §7 still lists 0x88000000; that VA sits inside
 * pil_cdsp_mem. Prefer this for Phase 3 kexec if 0x88000000 faults.
 */
#define STOS_KERNEL_LOAD_SAFE     0xA4000000ULL

#define STOS_HEAP_BASE            0x90000000ULL /* spec §7; overlaps PIL — use only after audit */

/* --- Phase B preview (not required for Phase 1) --- */

#define STOS_FB_BASE              0xA0000000ULL /* simple-framebuffer / cont_splash */
#define STOS_FB_SIZE              0x02300000ULL
#define STOS_FB_WIDTH             1080U
#define STOS_FB_HEIGHT            2520U
#define STOS_FB_STRIDE            (1080U * 4U)
#define STOS_TOUCH_I2C_BASE       0x00988000ULL /* i2c8 */
#define STOS_TOUCH_I2C_ADDR       0x48U         /* samsung,s6sy761 */
#define STOS_TOUCH_IRQ_GPIO       22U           /* TLMM */

/* GENI SE register offsets (Linux qcom-geni-se / qcom_geni_serial) */
#define STOS_GENI_TX_FIFOn        0x700U
#define STOS_GENI_TX_FIFO_STATUS  0x800U
#define STOS_GENI_TX_FIFO_WC_MASK 0x0FFFFFFFU

#define STOS_BOARD_NAME           "lena"
#define STOS_SOC_NAME             "sm6350"
#define STOS_DT_COMPAT            "sony,pdx213"

#ifndef __ASSEMBLER__
static inline int stos_addr_in_hyp_mem(unsigned long long pa)
{
	return pa >= STOS_HYP_MEM_BASE &&
	       pa < (STOS_HYP_MEM_BASE + STOS_HYP_MEM_SIZE);
}
#endif

#endif /* STOS_BSP_LENA_PDX213_H */
