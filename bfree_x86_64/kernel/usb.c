// --- USB HIDデバイス管理用 static 変数 ---
static usb_hid_priv_t *s_kbd_priv = NULL;
static usb_hid_priv_t *s_mouse_priv = NULL;

// --- usb_hid_priv_t取得ヘルパ ---
usb_hid_priv_t *get_usb_hid_priv(int is_keyboard) {
    return is_keyboard ? s_kbd_priv : s_mouse_priv;
}
// --- USB HIDデバイス（キーボード・マウス）管理構造体 ---
#define USB_HID_BUF_SIZE 64
typedef struct {
    uint8_t report[8]; // 最大8バイト: キーボード/マウス共通
} usb_hid_report_t;
typedef struct {
    usb_hid_report_t buf[USB_HID_BUF_SIZE];
    volatile int head;
    volatile int tail;
} usb_hid_ringbuf_t;

typedef struct {
    int dev_addr;
    int ep;
    usb_hid_ringbuf_t ringbuf;
} usb_hid_priv_t;

// --- HIDデバイス用 read関数 ---
static ssize_t usb_hid_read(void *dev, void *buf, size_t len) {
    usb_hid_priv_t *priv = (usb_hid_priv_t*)dev;
    if (len < sizeof(usb_hid_report_t)) return -1;
    if (priv->ringbuf.head == priv->ringbuf.tail) return 0;
    *(usb_hid_report_t*)buf = priv->ringbuf.buf[priv->ringbuf.tail];
    priv->ringbuf.tail = (priv->ringbuf.tail + 1) % USB_HID_BUF_SIZE;
    return sizeof(usb_hid_report_t);
}

// --- HIDデバイス登録関数（キーボード/マウス） ---
void register_usb_hid_device(int dev_addr, int ep, int is_keyboard) {
    static usb_hid_priv_t priv;
    priv.dev_addr = dev_addr;
    priv.ep = ep;
    priv.ringbuf.head = priv.ringbuf.tail = 0;
    if (is_keyboard) s_kbd_priv = &priv;
    else s_mouse_priv = &priv;
    static struct device_ops ops = {
        .read = usb_hid_read,
        .ioctl = NULL,
    };
    static device_t dev = {
        .name = NULL,
        .type = DEV_TYPE_CHAR,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    dev.name = is_keyboard ? "usbkbd0" : "usbmouse0";
    register_device(&dev);
    extern int devfs_register(const char *name, device_t *dev);
    devfs_register(dev.name, &dev);
}
// --- USBデバイス列挙・転送API雛形 ---
#include <stdint.h>
#include "device.h"


// --- USBコントローラ初期化雛形 ---
void usb_pci_scan_and_init(void) {
    // PCIバスをスキャンし、class_code==0x0C, subclass==0x03のデバイスを検出
    // EHCIコントローラのみ対応（例示）
    extern int pci_scan_class(uint8_t class_code, uint8_t subclass, uint8_t *bus, uint8_t *dev, uint8_t *func, uint32_t *bar);
    uint8_t bus, dev, func;
    uint32_t bar;
    if (pci_scan_class(0x0C, 0x03, &bus, &dev, &func, &bar) == 0) {
        volatile void *mmio_base = (volatile void *)(uintptr_t)(bar & ~0xF);
        register_usb_controller(mmio_base, 2); // 2:EHCI
    }
}

// --- USBコントローラ初期化・エンドポイント管理雛形 ---
void usb_controller_init(struct usb_ctrl_info *ctrl) {
    // EHCIのみ例示的に初期化
    if (ctrl->type == 2) { // EHCI
        volatile uint32_t *capbase = (volatile uint32_t*)ctrl->mmio_base;
        uint8_t caplength = *(volatile uint8_t*)capbase;
        volatile uint32_t *opbase = (volatile uint32_t*)((uintptr_t)ctrl->mmio_base + caplength);
        // コントローラリセット
        opbase[0] |= (1 << 1); // USBCMD.HCRESET
        for (volatile int i = 0; i < 100000; ++i) {}
        // ランタイムレジスタ初期化等（省略）
        // Root hubポート数取得
        uint8_t n_ports = (opbase[4] >> 0) & 0xFF;
        // 各ポートのデバイス検出
        for (int p = 0; p < n_ports; ++p) {
            // ポートリセット
            opbase[0x14/4 + p] |= (1 << 8); // PORTSCx.PRTRESET
            for (volatile int i = 0; i < 10000; ++i) {}
            opbase[0x14/4 + p] &= ~(1 << 8);
            // デバイス接続判定
            if (opbase[0x14/4 + p] & 1) {
                // デバイス接続あり: 制御転送でデスクリプタ取得等
                // usb_control_transfer()でデスクリプタ取得→HIDならregister_usb_hid_device呼び出し
                // ここでは例示的にキーボード/マウスを1つずつ登録
                if (p == 0) register_usb_hid_device(1, 0x81, 1); // 仮: dev_addr=1, ep=0x81, キーボード
                if (p == 1) register_usb_hid_device(2, 0x82, 0); // 仮: dev_addr=2, ep=0x82, マウス
            }
        }
    }
}

// --- USB転送処理雛形 ---
int usb_transfer(struct usb_ctrl_info *ctrl, int ep, void *buf, size_t len, int is_write) {
    // --- HID用インタラプト転送雛形 ---
    if (ep & 0x80) {
        // INエンドポイント（例: HIDキーボード/マウス）
        // 実際は転送リング/TDセットアップ・割り込み待ち
        // ここでは模擬的にバッファを0xCDで埋める
        memset(buf, 0xCD, len);
        return len;
    } else {
        // OUTエンドポイント（例: HIDレポート送信）
        // ここでは何もしない
        return len;
    }

    // --- USBストレージ用Bulk-Only Transport雛形 ---
    // SCSIコマンドをCBWで送信→データIN/OUT→CSW受信
    // 本来はCBW/CSW構造体・Bulk転送管理が必要
    // ここでは省略し、バッファを0xEFで埋める（IN時）
    if (/* ストレージクラス判定 */ 0) {
        if (!is_write) memset(buf, 0xEF, len);
        return len;
    }
    return 0;
}

int usb_enumerate_devices(void *dev) {
    // 仮実装: バス上のUSBデバイスを列挙
    // 今後: デバイスアドレス割当・HID/ストレージクラス対応
    (void)dev;
    return 0;
}

int usb_control_transfer(void *dev, int req_type, int req, int value, int index, void *data, int len) {
    // EHCI向け: 標準デバイスデスクリプタ取得などの制御転送本体
    struct usb_ctrl_info *ctrl = (struct usb_ctrl_info*)dev;
    if (ctrl->type != 2) return -1; // EHCIのみ例示
    volatile uint32_t *capbase = (volatile uint32_t*)ctrl->mmio_base;
    uint8_t caplength = *(volatile uint8_t*)capbase;
    volatile uint32_t *opbase = (volatile uint32_t*)((uintptr_t)ctrl->mmio_base + caplength);

    // --- SETUPパケット構築 ---
    struct setup_pkt {
        uint8_t bmRequestType;
        uint8_t bRequest;
        uint16_t wValue;
        uint16_t wIndex;
        uint16_t wLength;
    } __attribute__((packed));
    struct setup_pkt setup;
    setup.bmRequestType = req_type;
    setup.bRequest = req;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = len;

    // --- 転送リング/TDセットアップ（簡易: 1回分のみ） ---
    // 本来はQH/TD/リングバッファ管理が必要
    // ここでは省略し、DMAバッファにSETUP→IN/OUT→STATUSを直列で模擬
    static uint8_t dma_buf[256] __attribute__((aligned(64)));
    memcpy(dma_buf, &setup, sizeof(setup));
    if (!(req_type & 0x80) && data && len > 0) memcpy(dma_buf+8, data, len); // OUT

    // --- コントローラに転送要求（擬似: 実際はQH/TD登録とCIセット） ---
    // 本来はopbase[0x20/4]等でAsync List登録・Doorbell等
    // ここでは省略し、転送完了を即時模擬

    if ((req_type & 0x80) && data && len > 0) {
        // IN転送: データ受信を模擬
        memset(data, 0xAB, len); // 仮に0xABで埋める（本来はDMAバッファから）
    }

    // --- HID/ストレージクラス対応拡張ポイント ---
    // ここでbInterfaceClassを判定し、
    // ・HID: レポート転送/インタラプト転送
    // ・ストレージ: Bulk-Only Transport/CBW/CSW
    // などに分岐してusb_transfer()等を呼び出す
    // 例: if (bInterfaceClass == 3) { // HID
    //         return usb_transfer(ctrl, ep, data, len, 0);
    //     } else if (bInterfaceClass == 8) { // Mass Storage
    //         return usb_transfer(ctrl, ep, data, len, is_write);
    //     }
    return 0; // 成功
}
#include <stdint.h>
#include "device.h"

// USBコントローラ（XHCI/EHCI/OHCI/UHCI）雛形
// PCIスキャンでclass_code==0x0C, subclass==0x03


// --- I/Oリクエスト構造体とバッファ雛形 ---
typedef struct {
    void *buf;
    size_t len;
    int is_write;
    int completed;
    int result;
} usb_io_req_t;

#define USB_REQ_BUF_SIZE 16
typedef struct {
    usb_io_req_t reqs[USB_REQ_BUF_SIZE];
    volatile int head;
    volatile int tail;
} usb_req_ringbuf_t;

struct usb_ctrl_info {
    volatile void *mmio_base;
    int type; // 0:UHCI, 1:OHCI, 2:EHCI, 3:XHCI
    usb_req_ringbuf_t io_buf;
};
// --- USB割り込みハンドラ雛形 ---
void usb_irq_handler(void *regs) {
    (void)regs;
    // --- USB HIDインタラプト転送受信処理 ---
    for (int i = 0; i < 2; ++i) {
        usb_hid_priv_t *priv = get_usb_hid_priv(i == 0);
        if (!priv) continue;
        usb_hid_report_t rep;
        // 仮: usb_transferでレポート取得
        if (usb_transfer(NULL, priv->ep, &rep, sizeof(rep), 0) == sizeof(rep)) {
            int next = (priv->ringbuf.head + 1) % USB_HID_BUF_SIZE;
            if (next != priv->ringbuf.tail) {
                priv->ringbuf.buf[priv->ringbuf.head] = rep;
                priv->ringbuf.head = next;
            }
        }
    }
}


// --- USBコントローラ read/write/ioctl 雛形 ---
static int usb_open(void *dev, int mode) {
    // 必要なら初期化
    return 0;
}
static int usb_close(void *dev) {
    return 0;
}
static ssize_t usb_read(void *dev, void *buf, size_t len) {
    // TODO: USB転送でデータ受信
    (void)dev; (void)buf; (void)len;
    return 0;
}
static ssize_t usb_write(void *dev, const void *buf, size_t len) {
    // TODO: USB転送でデータ送信
    (void)dev; (void)buf; (void)len;
    return 0;
}
static int usb_ioctl(void *dev, int cmd, void *arg) {
    // 例: デバイス列挙/リセット/転送設定など
    (void)dev; (void)cmd; (void)arg;
    return 0;
}

void register_usb_controller(volatile void *mmio_base, int type) {
    static struct usb_ctrl_info priv;
    priv.mmio_base = mmio_base;
    priv.type = type;
    static struct device_ops ops = {
        .open = usb_open,
        .close = usb_close,
        .read = usb_read,
        .write = usb_write,
        .ioctl = usb_ioctl
    };
    static device_t usb_dev = {
        .name = "usb0",
        .type = DEV_TYPE_OTHER,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&usb_dev);
    // devfsにノード登録
    extern int devfs_register(const char *name, device_t *dev);
    devfs_register("usb0", &usb_dev);
    // 割り込みハンドラ登録（例: IRQはコントローラごとに異なる場合あり）
    // extern void register_irq_handler(int irq, void* handler);
    // register_irq_handler(..., usb_irq_handler);
}
