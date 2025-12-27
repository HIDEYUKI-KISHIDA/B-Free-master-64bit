// kernel_arm/device_arm.c
// ARM/64ビット用デバイスドライバ雛形（NIC/ストレージ/タイマ等）
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdint.h>
#include <stdio.h>

// --- NIC（ネットワークインタフェース）ドライバ雛形 ---

// NIC初期化
int nic_init_arm(void) {
    // QEMU仮想NIC(e1000)向けの初期化例（実際はI/OポートやMMIOアドレスに応じて実装）
    // ここでは雛形としてメッセージのみ
    printf("[NIC] nic_init_arm: QEMU e1000仮想NIC用初期化(雛形)\n");
    // 例: MMIO/IOポート初期化, MACアドレス設定, 割り込み有効化, DMAバッファ初期化など
    // 実際のアドレスやレジスタ仕様はQEMU/実機仕様に従うこと
    return 0;
}

// NIC送信
int nic_send_arm(const void *data, uint16_t len) {
    // QEMU仮想NIC(e1000)向けの送信例（雛形）
    printf("[NIC] nic_send_arm: QEMU e1000送信(雛形) len=%u\n", len);
    // 実際は送信バッファ/レジスタにdataを書き込む
    return 0;
}

// NIC受信
int nic_recv_arm(void *buf, uint16_t buflen) {
    // QEMU仮想NIC(e1000)向けの受信例（雛形）
    printf("[NIC] nic_recv_arm: QEMU e1000受信(雛形) buflen=%u\n", buflen);
    // 実際は受信バッファ/レジスタからデータを取得しbufへ格納
    return -1; // 仮: 受信なし
}

// --- ストレージ/ブロックデバイスドライバ雛形 ---
int storage_init_arm(void) {
    // ストレージコントローラ初期化（SD/eMMC/SATA/USB等）雛形
    // dma_init();
    // irq_init();
    // buffer_init();
    // 実機依存の初期化処理（QEMU/実ボード等）
    // 例: sdcard_init(), sata_init() など
    return 0;
}

int storage_read_arm(int dev, uint32_t lba, void *buf, uint32_t count) {
    // LBAアドレス指定でブロック読み出し雛形
    // dma_read(dev, lba, buf, count);
    // 割り込み待ち・バッファ管理
    // ファイルシステム連携（fs_read等から呼ばれる想定）
    return 0;
}

int storage_write_arm(int dev, uint32_t lba, const void *buf, uint32_t count) {
    // LBAアドレス指定でブロック書き込み雛形
    // dma_write(dev, lba, buf, count);
    // 割り込み待ち・バッファ管理
    // ファイルシステム連携（fs_write等から呼ばれる想定）
    return 0;
}
