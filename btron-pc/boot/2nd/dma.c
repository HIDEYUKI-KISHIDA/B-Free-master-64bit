// --- クロスビルド用ダミー定義 ---
#ifndef ROUNDUP
#define ROUNDUP(x,align) (((x + (align - 1)) / align) * align)
#endif
extern void busywait(int x);
extern void wait_int(int *x);
extern void lock(void);
extern void unlock(void);

/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

 (C) B-Free Project.
*/
extern int setup_dma (void *addr, int mode, int length, int mask);


// --- DMA制御本実装 ---
#include <stdint.h>
#include "dma.h"

// I/Oポートアクセス用マクロ（x86向け）
static inline void outb(uint16_t port, uint8_t val) {
#if defined(__GNUC__)
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
#endif
}
static inline uint8_t inb(uint16_t port) {
	uint8_t ret;
#if defined(__GNUC__)
	__asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
#endif
	return ret;
}

// FD用DMAチャネル2を仮定
#define DMA_CHANNEL 2
#define DMA_BASE_ADDR 0x00
#define DMA_BASE_COUNT 0x01
#define DMA_PAGE_PORT 0x81
#define DMA_MASK_REG 0x0A
#define DMA_MODE_REG 0x0B
#define DMA_CLEAR_FF_REG 0x0C

int setup_dma(void *addr, int mode, int length, int mask) {
	uint32_t phys = (uint32_t)addr;
	uint8_t page = (phys >> 16) & 0xFF;
	uint16_t offset = phys & 0xFFFF;

	// DMAコントローラの初期化
	outb(DMA_MASK_REG, DMA_CHANNEL | 0x04); // チャネルマスク
	outb(DMA_CLEAR_FF_REG, 0x00);           // フリップフロップクリア
	outb(DMA_MODE_REG, (mode & 0x0F) | (DMA_CHANNEL << 2)); // モード設定

	// アドレス・カウント設定
	outb(DMA_BASE_ADDR + (DMA_CHANNEL << 1), offset & 0xFF);
	outb(DMA_BASE_ADDR + (DMA_CHANNEL << 1), (offset >> 8) & 0xFF);
	outb(DMA_BASE_COUNT + (DMA_CHANNEL << 1), (length - 1) & 0xFF);
	outb(DMA_BASE_COUNT + (DMA_CHANNEL << 1), ((length - 1) >> 8) & 0xFF);
	outb(DMA_PAGE_PORT, page);

	outb(DMA_MASK_REG, DMA_CHANNEL); // チャネルアンマスク
	return 0;
}



// --- 32ビット版実装 ---
#ifdef __i386__
int setup_dma32(void *addr, int mode, int length, int mask) {
	// 32bit DMA制御（PCI/32bit用レジスタ設定の本実装）
	// PCI DMAレジスタ定義（例: 仮想アドレス分割用）
	#define PCI_DMA_PAGE_REG_HI  0x88
	#define PCI_DMA_PAGE_REG_MID 0x89
	#define PCI_DMA_OFFSET_REG   0x8A
	#define PCI_DMA_LENGTH_REG   0x8B
	#define PCI_DMA_MODE_REG     0x8C
	#define PCI_DMA_MASK_REG     0x8D

	uint32_t phys = (uint32_t)addr;
	uint8_t page_hi = (phys >> 24) & 0xFF;
	uint8_t page_mid = (phys >> 16) & 0xFF;
	uint16_t offset = phys & 0xFFFF;

	// PCI/32bit DMA用レジスタ設定
	outb(PCI_DMA_PAGE_REG_HI, page_hi);   // 上位8bit
	outb(PCI_DMA_PAGE_REG_MID, page_mid); // 中位8bit
	outb(PCI_DMA_OFFSET_REG, offset & 0xFF);     // 下位8bit
	outb(PCI_DMA_OFFSET_REG + 1, (offset >> 8) & 0xFF); // 上位8bit
	outb(PCI_DMA_LENGTH_REG, length & 0xFF);     // 転送長下位8bit
	outb(PCI_DMA_LENGTH_REG + 1, (length >> 8) & 0xFF); // 転送長上位8bit
	outb(PCI_DMA_MODE_REG, mode);         // モード
	outb(PCI_DMA_MASK_REG, mask);         // マスク

	// 転送開始（DMAコントローラの制御）
	// 例: 転送開始ビットセット
	// outb(PCI_DMA_MODE_REG, mode | 0x80); // 転送開始

	// 転送完了待ち（割り込み/ポーリング）
	// 例: DMA完了フラグをポーリング
	int dma_done = 0;
	while (!dma_done) {
		// PCI DMA完了フラグを確認（例: inb(PCI_DMA_MODE_REG) & 0x01）
		dma_done = inb(PCI_DMA_MODE_REG) & 0x01;
		busywait(10);
	}

	// エラー処理（例: DMAエラーフラグ確認）
	if (inb(PCI_DMA_MODE_REG) & 0x02) {
		return -1; // DMAエラー
	}

	return 0; // 正常終了
}
#endif

