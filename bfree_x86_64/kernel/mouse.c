#include "mouse.h"
#include <stdint.h>
#include "device.h"
#include "keyboard.h"

extern void uart_puts(const char *s);
extern void knl_pic_enable(int irq);

// シンプルなPS/2マウスドライバ雛形

typedef struct {
    int x, y;
    int buttons;
} mouse_state_t;

#define MOUSE_BUF_SIZE 64
typedef struct {
    mouse_state_t buf[MOUSE_BUF_SIZE];
    volatile int head;
    volatile int tail;
} mouse_ringbuf_t;

static mouse_state_t mouse_state;
static mouse_ringbuf_t mouse_buf = { .head = 0, .tail = 0 };
extern void event_publish_tk2_system(uintptr_t code, uintptr_t arg) __attribute__((weak));

// PS/2マウスI/Oポート
#define PS2_DATA  0x60
#define PS2_STATUS 0x64
#define PS2_CMD   0x64

#define PS2_STAT_OUT_FULL 0x01U
#define PS2_STAT_IN_FULL  0x02U
#define PS2_STAT_AUX_DATA  0x20U /* 1 = data from second PS/2 port (mouse) */

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void ps2_wait_write(void)
{
    uint32_t n = 0;
    while ((inb(PS2_STATUS) & PS2_STAT_IN_FULL) != 0 && n < 200000U) {
        ++n;
    }
}

static void ps2_wait_read(void)
{
    uint32_t n = 0;
    while ((inb(PS2_STATUS) & PS2_STAT_OUT_FULL) == 0 && n < 200000U) {
        ++n;
    }
}

static void ps2_cmd(uint8_t cmd)
{
    ps2_wait_write();
    outb(PS2_CMD, cmd);
}

static uint8_t ps2_read_data(void)
{
    ps2_wait_read();
    return inb(PS2_DATA);
}

static void ps2_mouse_write(uint8_t value)
{
    ps2_wait_write();
    outb(PS2_CMD, 0xD4U);
    ps2_wait_write();
    outb(PS2_DATA, value);
}

static int ps2_mouse_expect_ack(void)
{
    uint8_t r = ps2_read_data();
    return (r == 0xFAU) ? 0 : -1;
}

/* Enable PS/2 aux port + IRQ12 and start mouse data reporting (QEMU/Bochs friendly). */
static int ps2_mouse_hw_init(void)
{
    uint8_t cfg;
    int i;

    while ((inb(PS2_STATUS) & PS2_STAT_OUT_FULL) != 0) {
        (void)inb(PS2_DATA);
    }

    ps2_cmd(0xA8U);

    ps2_cmd(0x20U);
    cfg = ps2_read_data();
    cfg |= 2U;
    ps2_cmd(0x60U);
    ps2_wait_write();
    outb(PS2_DATA, cfg);

    for (i = 0; i < 3; ++i) {
        ps2_mouse_write(0xF4U);
        if (ps2_mouse_expect_ack() == 0) {
            return 0;
        }
    }
    return -1;
}

// --- リングバッファ操作関数 ---
static int mouse_ringbuf_is_empty(const mouse_ringbuf_t *rb) {
    return rb->head == rb->tail;
}
static int mouse_ringbuf_is_full(const mouse_ringbuf_t *rb) {
    return ((rb->head + 1) % MOUSE_BUF_SIZE) == rb->tail;
}
static void mouse_ringbuf_put(mouse_ringbuf_t *rb, mouse_state_t st) {
    if (!mouse_ringbuf_is_full(rb)) {
        rb->buf[rb->head] = st;
        rb->head = (rb->head + 1) % MOUSE_BUF_SIZE;
    }
}
static int mouse_ringbuf_get(mouse_ringbuf_t *rb, mouse_state_t *st) {
    if (!mouse_ringbuf_is_empty(rb)) {
        *st = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % MOUSE_BUF_SIZE;
        return 1;
    }
    return 0;
}

int mouse_has_data(void) {
    return !mouse_ringbuf_is_empty(&mouse_buf);
}

int mouse_pop_state(int *x, int *y, int *buttons) {
    mouse_state_t st;

    if (!mouse_ringbuf_get(&mouse_buf, &st)) {
        return 0;
    }

    if (x) *x = st.x;
    if (y) *y = st.y;
    if (buttons) *buttons = st.buttons;
    return 1;
}

/* 3バイト PS/2 ストリーム（IRQ と poll から共有） */
static uint8_t s_ps2_pkt[3];
static int s_ps2_idx;

static void mouse_feed_ps2_byte(uint8_t data)
{
    if (s_ps2_idx == 0) {
        if ((data & 0x08U) == 0U) {
            return;
        }
    }
    s_ps2_pkt[s_ps2_idx++] = data;
    if (s_ps2_idx < 3) {
        return;
    }
    s_ps2_idx = 0;
    mouse_state.x += (int8_t)s_ps2_pkt[1];
    /* PS/2 default Y sign is opposite framebuffer coords (origin top-left, +Y down). */
    mouse_state.y -= (int8_t)s_ps2_pkt[2];
    mouse_state.buttons = s_ps2_pkt[0] & 0x07;
    mouse_ringbuf_put(&mouse_buf, mouse_state);
    if (event_publish_tk2_system) {
        event_publish_tk2_system(2U, (uintptr_t)mouse_state.buttons);
    }
}

void mouse_poll_ps2(void)
{
    /* QEMU/empty controller: OUT_FULL can stick while DATA reads as 0 — unbounded
     * drain wedged guest input (QQmlEngine path: ppoll → coop_pump → BFreeInput). */
    int spins = 0;

    while ((inb(PS2_STATUS) & PS2_STAT_OUT_FULL) != 0) {
        uint8_t st;
        uint8_t data;

        if (++spins > 64) {
            break;
        }
        st = inb(PS2_STATUS);
        data = inb(PS2_DATA);
        if ((st & PS2_STAT_AUX_DATA) == 0) {
            keyboard_on_ps2_data(data);
            continue;
        }
        mouse_feed_ps2_byte(data);
    }
}

// --- マウス割り込みハンドラ本体 ---
void mouse_irq_handler(void *regs) {
    int spins = 0;

    (void)regs;
    while ((inb(PS2_STATUS) & PS2_STAT_OUT_FULL) != 0) {
        uint8_t st;
        uint8_t data;

        if (++spins > 64) {
            break;
        }
        st = inb(PS2_STATUS);
        data = inb(PS2_DATA);
        if ((st & PS2_STAT_AUX_DATA) == 0) {
            keyboard_on_ps2_data(data);
            continue;
        }
        mouse_feed_ps2_byte(data);
    }
}

ssize_t mouse_read(void *dev, void *buf, size_t len) {
    (void)dev;
    if (len < sizeof(mouse_state_t)) return -1;
    mouse_state_t st;
    if (mouse_ringbuf_get(&mouse_buf, &st)) {
        *(mouse_state_t*)buf = st;
        return sizeof(mouse_state_t);
    }
    return 0;
}

int mouse_ioctl(void *dev, int cmd, void *arg) {
    (void)dev;
    (void)cmd;
    (void)arg;
    // 今後: マウス設定・拡張
    return 0;
}

static device_ops_t mouse_ops = {
    .read = mouse_read,
    .ioctl = mouse_ioctl,
};

void mouse_init(void) {
    device_register("mouse0", &mouse_ops);
    // devfsにノード登録
    extern int devfs_register(const char *name, device_t *dev);
    extern device_t *find_device(const char *name);
    device_t *dev = find_device("mouse0");
    if (dev) devfs_register("mouse0", dev);
    // 割り込みハンドラ登録（例: IRQ12=44）
    extern void register_irq_handler(int irq, void* handler);
    register_irq_handler(44, mouse_irq_handler); // PS/2マウスIRQ12=44 (PIC+32)
    /* knl_pic_init() はスレーブ PIC を全マスクのまま — IRQ12 を開放 */
    knl_pic_enable(12);
    if (ps2_mouse_hw_init() != 0) {
        uart_puts("[MOUSE] PS/2 enable/stream failed (no movement expected)\n");
    } else {
        uart_puts("[MOUSE] PS/2 streaming on, IRQ12 unmasked\n");
    }
}
