// --- ネットワーク パケット送受信API雛形 ---
#include <stdint.h>
#include "device.h"


// --- PCIスキャン・MMIO初期化雛形 ---
void net_pci_scan_and_init(void) {
    // PCIバスをスキャンし、class_code==0x02, subclass==0x00(Ethernet)のデバイスを検出
    extern int pci_scan_class(uint8_t class_code, uint8_t subclass, uint8_t *bus, uint8_t *dev, uint8_t *func, uint32_t *bar);
    uint8_t bus, dev, func;
    uint32_t bar;
    extern int pci_scan_vendor_device(uint16_t vendor, uint16_t device, uint8_t *bus, uint8_t *dev, uint8_t *func, uint32_t *bar);
    // Intel E1000
    uint16_t e1000_devs[] = {0x100E, 0x1019, 0x100F};
    for (int i = 0; i < 3; ++i) {
        if (pci_scan_vendor_device(0x8086, e1000_devs[i], &bus, &dev, &func, &bar) == 0) {
            volatile void *mmio_base = (volatile void *)(uintptr_t)(bar & ~0xF);
            register_net_device(mmio_base, 0x8086, e1000_devs[i], 0); // type=0:Ethernet
            return;
        }
    }
    // Realtek RTL8139/8169
    uint16_t rtl_devs[] = {0x8139, 0x8168, 0x8169};
    for (int i = 0; i < 3; ++i) {
        if (pci_scan_vendor_device(0x10EC, rtl_devs[i], &bus, &dev, &func, &bar) == 0) {
            volatile void *mmio_base = (volatile void *)(uintptr_t)(bar & ~0xF);
            register_net_device(mmio_base, 0x10EC, rtl_devs[i], 0); // type=0:Ethernet
            return;
        }
    }
    // Virtio-net (0x1AF4: vendor, 0x1000: device)
    if (pci_scan_vendor_device(0x1AF4, 0x1000, &bus, &dev, &func, &bar) == 0) {
        volatile void *mmio_base = (volatile void *)(uintptr_t)(bar & ~0xF);
        register_net_device(mmio_base, 0x1AF4, 0x1000, 0); // type=0:Ethernet
        return;
    }
}
// --- IPスタック連携用フック関数 ---
// 受信パケットをIP層に渡す（カーネル内IPスタック用）
void net_ip_input(const uint8_t *pkt, size_t len) {
    // ここでIP/ARP/ICMP等のプロトコル処理に渡す
    // 例: ip_input(pkt, len);
}

// IP層から送信パケットを受け取る（カーネル内IPスタック用）
void net_ip_output(const uint8_t *pkt, size_t len) {
    // ここでnet_write等を呼び出しNICから送信
    net_write(NULL, pkt, len);
}

// --- NIC初期化・DMA/割り込み雛形 ---
void net_nic_init(struct netdev_info *nic) {
    // Intel E1000向け簡易初期化例
    volatile uint32_t *regs = (volatile uint32_t*)nic->mmio_base;
    // 1. MACアドレス取得（RA[0]/RA[1]）
    uint32_t ral = regs[0x5400/4];
    uint32_t rah = regs[0x5404/4];
    uint8_t mac[6];
    mac[0] = ral & 0xFF; mac[1] = (ral >> 8) & 0xFF; mac[2] = (ral >> 16) & 0xFF; mac[3] = (ral >> 24) & 0xFF;
    mac[4] = rah & 0xFF; mac[5] = (rah >> 8) & 0xFF;
    // 2. 受信バッファ・送信バッファ初期化（省略: 本来はDMAバッファ物理アドレス設定）
    // 3. 割り込み有効化（省略: IMSレジスタ等）
    // 4. NIC有効化（省略: CTRLレジスタ等）
    // ...
    (void)mac;
}

// --- パケット送受信本格雛形 ---
int net_dma_send(struct netdev_info *nic, const void *data, size_t len) {
    // NICのDMA送信リングにパケットを投入し送信
    (void)nic; (void)data; (void)len;
    return 0;
}
int net_dma_recv(struct netdev_info *nic, void *buf, size_t maxlen) {
    // NICのDMA受信リングからパケットを取得
    (void)nic; (void)buf; (void)maxlen;
    return 0;
}


// --- パケットリングバッファ定義 ---
#define NET_PKT_BUF_SIZE 16
#define NET_PKT_MAX_LEN 2048
typedef struct {
    uint8_t data[NET_PKT_MAX_LEN];
    size_t len;
} net_packet_t;
typedef struct {
    net_packet_t pkts[NET_PKT_BUF_SIZE];
    volatile int head;
    volatile int tail;
} net_pkt_ringbuf_t;

static net_pkt_ringbuf_t rx_ring = { .head = 0, .tail = 0 };
static net_pkt_ringbuf_t tx_ring = { .head = 0, .tail = 0 };
// --- ネットワーク割り込みハンドラ雛形 ---
void net_irq_handler(void *regs) {
    (void)regs;
    // Intel E1000向け受信割り込み例（省略: 実際はDESC/バッファ管理）
    // 仮に1パケット受信したとみなしてrx_ringに格納
    int next = (rx_ring.head + 1) % NET_PKT_BUF_SIZE;
    if (next != rx_ring.tail) {
        net_packet_t *pkt = &rx_ring.pkts[rx_ring.head];
        pkt->len = 64; // 仮: 64バイト
        for (int i = 0; i < 64; ++i) pkt->data[i] = 0xAA; // 仮データ
        rx_ring.head = next;
    }
}

int net_send_packet(void *dev, const void *data, size_t len) {
    // 仮実装: dataをパケットとして送信
    // 今後: NICレジスタに書き込み、送信DMA等
    (void)dev;
    if (len > sizeof(tx_buffer)) return -1;
    for (size_t i = 0; i < len; ++i) tx_buffer[i] = ((const uint8_t*)data)[i];
    return (int)len;
}

int net_recv_packet(void *dev, void *buf, size_t maxlen) {
    // 仮実装: 受信パケットをbufに格納
    // 今後: NICレジスタから受信、受信DMA等
    (void)dev;
    size_t n = (maxlen < sizeof(rx_buffer)) ? maxlen : sizeof(rx_buffer);
    for (size_t i = 0; i < n; ++i) ((uint8_t*)buf)[i] = rx_buffer[i];
    return (int)n;
}
#include <stdint.h>
#include "device.h"

// ネットワークコントローラ（Ethernet/Wi-Fi）雛形
// PCIスキャンでclass_code==0x02, subclass==0x00(Ethernet), 0x80(その他)

struct netdev_info {
    volatile void *mmio_base;
    uint16_t vendor_id, device_id;
    int type; // 0:Ethernet, 1:Wi-Fi, 2:Other
};


// --- ネットワーク read/write/ioctl 雛形 ---
static int net_open(void *dev, int mode) {
    // 必要なら初期化
    return 0;
}
static int net_close(void *dev) {
    return 0;
}
static ssize_t net_read(void *dev, void *buf, size_t len) {
    // rx_ringから受信パケットを取得
    if (rx_ring.head == rx_ring.tail) return 0;
    net_packet_t *pkt = &rx_ring.pkts[rx_ring.tail];
    size_t n = (len < pkt->len) ? len : pkt->len;
    for (size_t i = 0; i < n; ++i) ((uint8_t*)buf)[i] = pkt->data[i];
    rx_ring.tail = (rx_ring.tail + 1) % NET_PKT_BUF_SIZE;
    return n;
}
static ssize_t net_write(void *dev, const void *buf, size_t len) {
    // Intel E1000向け送信DMA例（省略: 実際はDESC/バッファ管理）
    // 仮に送信成功とみなす
    (void)dev;
    if (len > NET_PKT_MAX_LEN) return -1;
    return (ssize_t)len;
}
static int net_ioctl(void *dev, int cmd, void *arg) {
    // 例: MACアドレス取得/設定、リンク状態取得など
    (void)dev; (void)cmd; (void)arg;
    return 0;
}

void register_net_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type) {
    static struct netdev_info priv;
    priv.mmio_base = mmio_base;
    priv.vendor_id = vendor_id;
    priv.device_id = device_id;
    priv.type = type;
    static struct device_ops ops = {
        .open = net_open,
        .close = net_close,
        .read = net_read,
        .write = net_write,
        .ioctl = net_ioctl
    };
    static device_t net_dev = {
        .name = "net0",
        .type = DEV_TYPE_OTHER,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&net_dev);
    // devfsにノード登録
    extern int devfs_register(const char *name, device_t *dev);
    devfs_register("net0", &net_dev);
    // 割り込みハンドラ登録（例: IRQはNICごとに異なる場合あり）
    // extern void register_irq_handler(int irq, void* handler);
    // register_irq_handler(..., net_irq_handler);
}
