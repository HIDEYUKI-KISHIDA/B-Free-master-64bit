// --- PCI BARからABAR取得・全ポート初期化 ---
#include <stdio.h>
#define AHCI_MAX_PORTS 32

typedef struct {
    uint32_t cap;      // Host Capabilities
    uint32_t ghc;      // Global Host Control
    uint32_t is;       // Interrupt Status
    uint32_t pi;       // Ports Implemented
    uint32_t vs;       // Version
    uint32_t ccc_ctl;  // Command Completion Coalescing Control
    uint32_t ccc_pts;  // Command Completion Coalescing Ports
    uint32_t em_loc;   // Enclosure Management Location
    uint32_t em_ctl;   // Enclosure Management Control
    uint32_t cap2;     // Host Capabilities Extended
    uint32_t bohc;     // BIOS/OS Handoff Control and Status
    uint8_t  reserved[0xA0-0x2C];
    // ...
} ahci_abar_t;

void ahci_controller_init(volatile ahci_abar_t *abar) {
    uint32_t pi = abar->pi;
    for (int port = 0; port < AHCI_MAX_PORTS; ++port) {
        if (!(pi & (1 << port))) continue; // 実装されていないポートはスキップ
        volatile ahci_port_reg_t *portreg = (volatile ahci_port_reg_t *)((uintptr_t)abar + 0x100 + port * sizeof(ahci_port_reg_t));
        // コマンドエンジン停止
        portreg->cmd &= ~0x01; // STP
        // 停止完了待ち（本来はタイムアウト付きループ推奨）
        while (portreg->cmd & 0x4000) {}
        // 必要な初期化処理（バッファ割当・FIS/CLBセット等）
        // ...
        // コマンドエンジン開始
        portreg->cmd |= 0x01; // ST
    }
    printf("AHCI controller initialized.\n");
}
// --- AHCI LBA単位ブロックI/O本格雛形 ---
#include <stddef.h>
#include <string.h>
#define AHCI_PORT_REG_BASE 0x100
// AHCIレジスタ構造体雛形
typedef struct {
    uint32_t clb;    // コマンドリストベース
    uint32_t clbu;
    uint32_t fb;     // FISベース
    uint32_t fbu;
    uint32_t is;     // 割り込みステータス
    uint32_t ie;     // 割り込み有効
    uint32_t cmd;    // コマンド
    uint32_t reserved0;
    uint32_t tfd;    // タスクファイルデータ
    uint32_t sig;    // シグネチャ
    uint32_t ssts;   // SATAステータス
    uint32_t sctl;   // SATAコントロール
    uint32_t serr;   // SATAエラー
    uint32_t sact;   // アクティブ
    uint32_t ci;     // コマンド発行
    uint32_t sntf;   // SNotification
    uint32_t fbs;    // FISベーススイッチ
    uint32_t reserved1[11];
} ahci_port_reg_t;

// AHCI初期化雛形
void ahci_port_init(volatile ahci_port_reg_t *port) {
    // コマンドエンジン停止→初期化→開始
    port->cmd &= ~0x01; // STP
    // ...必要な初期化処理...
    port->cmd |= 0x01;  // ST
}

// AHCIコマンド発行雛形
int ahci_issue_cmd(volatile ahci_port_reg_t *port, int is_write, uint64_t lba, void *buf, size_t len) {
    // --- AHCIコマンドリスト/テーブル/PRDTセットアップとDMA転送 ---
    // 1. コマンドリスト/テーブル/PRDT用のメモリ領域を確保（簡易: 静的バッファ）
    static uint8_t clb[1024] __attribute__((aligned(1024))); // 32エントリ×32B
    static uint8_t ctba[256*32] __attribute__((aligned(128))); // 32エントリ×256B
    static uint8_t fis[256] __attribute__((aligned(256)));
    memset(clb, 0, sizeof(clb));
    memset(ctba, 0, sizeof(ctba));
    memset(fis, 0, sizeof(fis));

    // 2. ポートにCLB/FBアドレスを設定
    port->clb = (uint32_t)(uintptr_t)clb;
    port->clbu = (uint32_t)(((uintptr_t)clb) >> 32);
    port->fb = (uint32_t)(uintptr_t)fis;
    port->fbu = (uint32_t)(((uintptr_t)fis) >> 32);

    // 3. コマンドリストエントリ構造体
    typedef struct {
        uint16_t flags; // PRDT長/属性
        uint16_t prdtl;
        uint32_t prdbc;
        uint32_t ctba;
        uint32_t ctbau;
        uint32_t reserved[4];
    } ahci_cmd_header_t;
    ahci_cmd_header_t *cmd_list = (ahci_cmd_header_t*)clb;

    // 4. コマンドテーブル構造体
    typedef struct {
        uint8_t cfis[64];
        uint8_t acmd[16];
        uint8_t reserved[48];
        struct {
            uint32_t dba;
            uint32_t dbau;
            uint32_t reserved0;
            uint32_t dbc; // (byte count - 1) | (I=interrupt)
        } prdt[1]; // 1エントリのみ
    } ahci_cmd_tbl_t;
    ahci_cmd_tbl_t *cmd_tbl = (ahci_cmd_tbl_t*)ctba;

    // 5. PRDTセットアップ
    cmd_list[0].prdtl = 1;
    cmd_list[0].ctba = (uint32_t)(uintptr_t)cmd_tbl;
    cmd_list[0].ctbau = (uint32_t)(((uintptr_t)cmd_tbl) >> 32);
    cmd_tbl->prdt[0].dba = (uint32_t)(uintptr_t)buf;
    cmd_tbl->prdt[0].dbau = (uint32_t)(((uintptr_t)buf) >> 32);
    cmd_tbl->prdt[0].dbc = (uint32_t)(len - 1); // 512B固定
    cmd_tbl->prdt[0].reserved0 = 0;

    // 6. FISセットアップ（Register Host to Device FIS）
    uint8_t *cfis = cmd_tbl->cfis;
    cfis[0] = 0x27; // FIS Type: Register H2D
    cfis[1] = (1 << 7); // Command
    cfis[2] = is_write ? 0x35 : 0x25; // ATA WRITE DMA EXT / READ DMA EXT
    cfis[3] = 0;
    cfis[4] = (uint8_t)(lba);
    cfis[5] = (uint8_t)(lba >> 8);
    cfis[6] = (uint8_t)(lba >> 16);
    cfis[7] = 0;
    cfis[8] = (uint8_t)(lba >> 24);
    cfis[9] = (uint8_t)(lba >> 32);
    cfis[10] = (uint8_t)(lba >> 40);
    cfis[11] = 0;
    cfis[12] = 1; // sector count (1セクタ)
    cfis[13] = 0;
    cfis[14] = 0;
    cfis[15] = 0;

    // 7. コマンド発行
    port->is = (uint32_t)-1; // 割り込みクリア
    port->ci = 1; // コマンド0発行

    // 8. 完了待ち（ciビットが0になるまで）
    int timeout = 1000000;
    while ((port->ci & 1) && --timeout > 0) {}
    if (timeout <= 0) return -1; // タイムアウト

    // 9. エラーチェック（tfdビット）
    if (port->tfd & 0x1) return -2; // ERR
    if (port->tfd & 0x8) return -3; // DF

    return 0;
}

int ahci_read_sector(void *dev, uint64_t lba, void *buf) {
    struct ahci_port_info *info = (struct ahci_port_info*)dev;
    volatile ahci_port_reg_t *port = (volatile ahci_port_reg_t *)((uintptr_t)info->abar + AHCI_PORT_REG_BASE + info->port_num * sizeof(ahci_port_reg_t));
    return ahci_issue_cmd(port, 0, lba, buf, 512);
}

int ahci_write_sector(void *dev, uint64_t lba, const void *buf) {
    struct ahci_port_info *info = (struct ahci_port_info*)dev;
    volatile ahci_port_reg_t *port = (volatile ahci_port_reg_t *)((uintptr_t)info->abar + AHCI_PORT_REG_BASE + info->port_num * sizeof(ahci_port_reg_t));
    return ahci_issue_cmd(port, 1, lba, (void*)buf, 512);
}
#include <stdint.h>
#include "device.h"

// AHCI/Serial ATAストレージデバイス雛形
// PCIスキャンでclass_code==0x01, subclass==0x06ならAHCI

// --- I/Oリクエスト構造体とバッファ雛形 ---
typedef struct {
    uint64_t lba;
    void *buf;
    size_t len;
    int is_write;
    int completed;
    int result;
} ahci_io_req_t;

#define AHCI_REQ_BUF_SIZE 16
typedef struct {
    ahci_io_req_t reqs[AHCI_REQ_BUF_SIZE];
    volatile int head;
    volatile int tail;
} ahci_req_ringbuf_t;

struct ahci_port_info {
    volatile void *abar; // AHCI Base Address
    int port_num;
    ahci_req_ringbuf_t io_buf;
};
// --- AHCI割り込みハンドラ雛形 ---
void ahci_irq_handler(void *regs) {
    (void)regs;
    // TODO: AHCI割り込み処理（完了通知・バッファ管理）
}

// --- AHCIストレージ read/write/ioctl 雛形 ---
static int ahci_open(void *dev, int mode) {
    // 必要なら初期化
    return 0;
}
static int ahci_close(void *dev) {
    return 0;
}
static ssize_t ahci_read(void *dev, void *buf, size_t len) {
    // 512バイト単位で読み出し
    size_t n = 0;
    uint8_t *p = buf;
    for (; n + 512 <= len; n += 512) {
        if (ahci_read_sector(dev, n/512, p + n) != 0) break;
    }
    return n;
}
static ssize_t ahci_write(void *dev, const void *buf, size_t len) {
    // 512バイト単位で書き込み
    size_t n = 0;
    const uint8_t *p = buf;
    for (; n + 512 <= len; n += 512) {
        if (ahci_write_sector(dev, n/512, p + n) != 0) break;
    }
    return n;
}
static int ahci_ioctl(void *dev, int cmd, void *arg) {
    // 例: 容量取得/キャッシュ制御/SMART情報取得など
    (void)dev; (void)cmd; (void)arg;
    return 0;
}

// AHCIデバイス登録（PCIスキャンから呼び出し想定）
void register_ahci_device(volatile void *abar, int port_num) {
    static struct ahci_port_info priv;
    priv.abar = abar;
    priv.port_num = port_num;
    priv.io_buf.head = 0;
    priv.io_buf.tail = 0;
    priv.port_num = port_num;
    static struct device_ops ops = {
        .open = ahci_open,
        .close = ahci_close,
        .read = ahci_read,
        .write = ahci_write,
        .ioctl = ahci_ioctl
    };
    static device_t ahci_dev = {
        .name = "sata0",
        .type = DEV_TYPE_BLOCK,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&ahci_dev);
    // devfsにノード登録
    extern int devfs_register(const char *name, device_t *dev);
    devfs_register("sata0", &ahci_dev);
    // 割り込みハンドラ登録（例: IRQはポートごとに異なる場合あり）
    // extern void register_irq_handler(int irq, void* handler);
    // register_irq_handler(..., ahci_irq_handler);
}
