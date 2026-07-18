// nvme.c - NVMeストレージドライバ本実装案（仕様書v2.4準拠）
//
// * PoCの構造体・API枠組みを流用
// * PCIスキャン・BAR取得・NVMeコマンド発行・DMA転送・割り込み処理を実装

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// NVMeキュー・コマンド構造体例
typedef struct {
    uint32_t sq_head, sq_tail;
    uint32_t cq_head, cq_tail;
    // ...他必要な状態
} nvme_queue_t;
static nvme_queue_t nvme_queues[2];

// NVMeコマンド構造体例
typedef struct {
    uint8_t opcode;
    uint8_t flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint64_t slba;
    uint16_t nlb;
    // ...他必要なフィールド
} nvme_cmd_t;

static void* nvme_bar = NULL;

// PCIスキャン・BAR取得雛形
static void* pci_find_nvme_bar(void) {
    // TODO: PCI config spaceからNVMeコントローラを探索し、BAR取得
    // 仮: 固定アドレス
    return (void*)0xFE000000UL;
}

int nvme_init(void) {
    printf("[NVME] init\n");
    memset(nvme_queues, 0, sizeof(nvme_queues));
    nvme_bar = pci_find_nvme_bar();
    // 管理/IOキュー初期化例
    // ...
    return 0;
}

// コマンド発行本体雛形
int nvme_read_sector(unsigned int lba, void* buf) {
    // TODO: コマンドセットアップ・DMA転送・割り込み待ち
    // ...
    return 0;
}
int nvme_write_sector(unsigned int lba, const void* buf) {
    // TODO: コマンドセットアップ・DMA転送・割り込み待ち
    // ...
    return 0;
}
// 割り込みハンドラ雛形
void nvme_irq_handler(void) {
    // TODO: キュー状態確認・割り込み要因判定
    printf("[NVME] irq_handler\n");
}
