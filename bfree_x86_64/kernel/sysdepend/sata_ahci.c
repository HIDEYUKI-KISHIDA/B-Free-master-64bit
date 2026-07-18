// sata_ahci.c - SATA(AHCI)ドライバ本実装案（仕様書v2.4準拠）
//
// * PoCの構造体・API枠組みを流用
// * PCIスキャン・BAR取得・HBA初期化・DMA転送・割り込み処理を実装

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// AHCIレジスタ構造体例
typedef struct {
    volatile uint32_t cap;
    volatile uint32_t ghc;
    volatile uint32_t is;
    // ...他必要なレジスタ
} ahci_hba_t;

// ポート・コマンド管理構造体例
typedef struct {
    int port_num;
    int device_present;
    // ...他必要な状態
} ahci_port_t;
static ahci_port_t ahci_ports[4];

static ahci_hba_t* hba = NULL;

// PCIスキャン・BAR取得雛形
static void* pci_find_ahci_bar(void) {
    // TODO: PCI config spaceからクラスコード/ベンダID等でAHCIコントローラを探索し、BAR取得
    // 仮: 固定アドレス
    return (void*)0xFEC00000UL;
}

int sata_ahci_init(void) {
    printf("[SATA_AHCI] init\n");
    memset(ahci_ports, 0, sizeof(ahci_ports));
    hba = (ahci_hba_t*)pci_find_ahci_bar();
    // HBAレジスタ初期化例
    if (hba) {
        hba->ghc |= 0x01; // GHC.AE: AHCI有効化
        // ポート検出・初期化
        // ...
    }
    return 0;
}

// コマンド発行本体雛形
int sata_ahci_read_sector(int port, unsigned int lba, void* buf) {
    // TODO: コマンドリスト・FISセットアップ・DMA転送・割り込み待ち
    // ...
    return 0;
}
int sata_ahci_write_sector(int port, unsigned int lba, const void* buf) {
    // TODO: コマンドリスト・FISセットアップ・DMA転送・割り込み待ち
    // ...
    return 0;
}
// 割り込みハンドラ雛形
void sata_ahci_irq_handler(void) {
    // TODO: HBA->is等で割り込み要因判定・EOI送出
    printf("[SATA_AHCI] irq_handler\n");
}
