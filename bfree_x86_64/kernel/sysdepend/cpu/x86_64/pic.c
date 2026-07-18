/*
 * pic.c - x86_64 T-Kernel2.0 8259A PIC制御
 * 仕様: TK2_x86_64_Spec.md v2.3 5.6節準拠
 */
#include <stdint.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void io_wait(void) {
    /* ポート0x80へのダミー書き込みでウェイト */
    outb(0x80, 0);
}

void knl_pic_init(void) {
    /* ICW1: 初期化要求 */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    /* ICW2: 割込みベクタオフセット */
    outb(PIC1_DATA, 0x20); io_wait(); // Master: 0x20
    outb(PIC2_DATA, 0x28); io_wait(); // Slave: 0x28
    /* ICW3: カスケード接続 */
    outb(PIC1_DATA, 0x04); io_wait(); // SlaveはIR2
    outb(PIC2_DATA, 0x02); io_wait(); // Slave ID=2
    /* ICW4: 8086/88モード */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();
    /* 全割込みマスク（初期化直後）→IRQ1だけ許可 */
    outb(PIC1_DATA, 0xFD); io_wait(); // 0b11111101: IRQ1(キーボード)だけ許可
    outb(PIC2_DATA, 0xFF); io_wait();
}

void knl_pic_enable(int irq) {
    uint16_t port;
    uint8_t mask;
    if (irq < 8) {
        port = PIC1_DATA;
        mask = inb(port) & ~(1 << irq);
        outb(port, mask);
    } else {
        port = PIC2_DATA;
        mask = inb(port) & ~(1 << (irq - 8));
        outb(port, mask);
    }
}

void knl_pic_eoi(int irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20); // Slave PICへEOI
    }
    outb(PIC1_CMD, 0x20);     // Master PICへEOI
}

// main.c から呼ばれるラッパー
void pic_init(void) {
    knl_pic_init();
}
