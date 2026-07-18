#include "device.h"
#include <stdint.h>

// I/Oポートアクセスラッパ
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// シリアルポート（COM1）ベースアドレス
#define SERIAL_PORT 0x3F8

// --- リングバッファ定義 ---
#define SERIAL_BUF_SIZE 256
typedef struct {
    char buf[SERIAL_BUF_SIZE];
    volatile int head;
    volatile int tail;
} ringbuf_t;

// シリアルデバイス固有データ
struct serial_priv {
    uint16_t port_base;
    ringbuf_t rx_buf; // 受信バッファ
    ringbuf_t tx_buf; // 送信バッファ
};
// --- リングバッファ操作関数 ---
static int ringbuf_is_empty(const ringbuf_t *rb) {
    return rb->head == rb->tail;
}
static int ringbuf_is_full(const ringbuf_t *rb) {
    return ((rb->head + 1) % SERIAL_BUF_SIZE) == rb->tail;
}
static void ringbuf_put(ringbuf_t *rb, char c) {
    if (!ringbuf_is_full(rb)) {
        rb->buf[rb->head] = c;
        rb->head = (rb->head + 1) % SERIAL_BUF_SIZE;
    }
}
static int ringbuf_get(ringbuf_t *rb, char *c) {
    if (!ringbuf_is_empty(rb)) {
        *c = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % SERIAL_BUF_SIZE;
        return 1;
    }
    return 0;
}

// --- シリアル割り込みハンドラ本体 ---
void serial_irq_handler(void *regs) {
    (void)regs;
    // COM1固定
    uint16_t port = SERIAL_PORT;
    // device_t取得（単純化: グローバル登録のみ想定）
    extern device_t *find_device(const char *name);
    device_t *dev = find_device("serial0");
    if (!dev) return;
    struct serial_priv *priv = (struct serial_priv*)dev->priv;
    // 割り込み原因判定
    uint8_t iir = inb(port + 2);
    if (iir & 0x01) return; // 割り込みなし
    uint8_t cause = (iir >> 1) & 0x07;
    if (cause == 0x02) { // 送信バッファ空
        char c;
        if (ringbuf_get(&priv->tx_buf, &c)) {
            outb(port, c);
        }
    } else if (cause == 0x04 || cause == 0x0C) { // 受信データあり
        char c = inb(port);
        ringbuf_put(&priv->rx_buf, c);
    }
}

// open/close/read/write/ioctlの最小実装
static int serial_open(void *dev, int mode) {
    struct serial_priv *priv = ((device_t*)dev)->priv;
    // 8N1, 115200bps, FIFO有効化など最低限の初期化
    outb(priv->port_base + 1, 0x00); // 割り込み禁止
    outb(priv->port_base + 3, 0x80); // DLAB=1
    outb(priv->port_base + 0, 0x01); // 115200bps (divisor=1)
    outb(priv->port_base + 1, 0x00);
    outb(priv->port_base + 3, 0x03); // 8N1, DLAB=0
    outb(priv->port_base + 2, 0xC7); // FIFO有効化
    outb(priv->port_base + 4, 0x0B); // OUT2,RTS,DTR
    return 0;
}
static int serial_close(void *dev) {
    (void)dev;
    return 0;
}
static ssize_t serial_write(void *dev, const void *buf, size_t len) {
    struct serial_priv *priv = ((device_t*)dev)->priv;
    const char *p = buf;
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) {
        if (!ringbuf_is_full(&priv->tx_buf)) {
            ringbuf_put(&priv->tx_buf, p[i]);
            n++;
        } else {
            break;
        }
    }
    // 送信バッファ空なら即送信開始
    if (inb(priv->port_base + 5) & 0x20) {
        char c;
        if (ringbuf_get(&priv->tx_buf, &c)) {
            outb(priv->port_base, c);
        }
    }
    // 割り込み有効化（送信）
    outb(priv->port_base + 1, 0x03); // 受信・送信割り込み許可
    return (ssize_t)n;
}
static ssize_t serial_read(void *dev, void *buf, size_t len) {
    struct serial_priv *priv = ((device_t*)dev)->priv;
    char *p = buf;
    size_t n = 0;
    while (n < len) {
        if (ringbuf_get(&priv->rx_buf, &p[n])) {
            n++;
        } else {
            break;
        }
    }
    return (ssize_t)n;
}
static int serial_ioctl(void *dev, int cmd, void *arg) {
    (void)dev; (void)cmd; (void)arg;
    return 0;
}

// シリアルデバイス登録関数
void register_serial_device(void) {
    static struct serial_priv priv = {
        .port_base = SERIAL_PORT,
        .rx_buf = { .head = 0, .tail = 0 },
        .tx_buf = { .head = 0, .tail = 0 }
    };
    static struct device_ops ops = {
        .open = serial_open,
        .close = serial_close,
        .read = serial_read,
        .write = serial_write,
        .ioctl = serial_ioctl
    };
    static device_t serial_dev = {
        .name = "serial0",
        .type = DEV_TYPE_CHAR,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&serial_dev);
    // devfsにノード登録
    extern int devfs_register(const char *name, device_t *dev);
    devfs_register("serial0", &serial_dev);
    // 割り込みハンドラ登録（例: IRQ4）
    extern void register_irq_handler(int irq, void* handler);
    register_irq_handler(36, serial_irq_handler); // COM1=IRQ4=36 (PIC+32)
}
