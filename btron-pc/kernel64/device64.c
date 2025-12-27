// device64.c - 64ビット用デバイス初期化雛形
#include "../include64/types.h"



// VGA初期化（本物雰囲気）
void vga_init64(void) {
    // 雰囲気: VGAテキストモード初期化
    outb64(0x3D4, 0x0A); // カーソル非表示
    outb64(0x3D5, 0x20);
}

// タイマー初期化（本物雰囲気）
void timer_init64(void) {
    // 雰囲気: PIT(8253)で100Hz割り込み
    unsigned int divisor = 1193180 / 100;
    outb64(0x43, 0x36);
    outb64(0x40, divisor & 0xFF);
    outb64(0x40, (divisor >> 8) & 0xFF);
    // 割り込みハンドラ登録（雰囲気）
    extern int register_device_driver64(const char*, int(*)(void*), int);
    register_device_driver64("timer", timer_isr64, 32);
}

// タイマー割り込みハンドラ（雰囲気）
int timer_isr64(void *arg) {
    extern void printk64(const char*);
    printk64("[TIMER] Tick\n");
    return 0;
}

// キーボード初期化（本物雰囲気）
void keyboard_init64(void) {
    // 雰囲気: 割り込み有効化
    outb64(0x21, inb64(0x21) & ~0x02); // IRQ1許可
    // 割り込みハンドラ登録（雰囲気）
    extern int register_device_driver64(const char*, int(*)(void*), int);
    register_device_driver64("keyboard", keyboard_isr64, 33);
}

// キーボード割り込みハンドラ（雰囲気）
int keyboard_isr64(void *arg) {
    extern void printk64(const char*);
    printk64("[KEYBOARD] Key event\n");
    return 0;
}

void init_device64(void) {
    vga_init64();
    timer_init64();
    keyboard_init64();
    // 他デバイスも必要に応じて追加
}


// --- 追加: デバイス管理APIの雰囲気を再現（ダミー実装） ---

#define MAX_DEVICE64 8
typedef struct {
    const char *name;
    int state; // 0:OFF, 1:ON
} device_entry64_t;

static device_entry64_t device_table64[MAX_DEVICE64] = {
    {"vga", 0},
    {"timer", 0},
    {"keyboard", 0},
    {"fd", 0},
    {"ide", 0},
    {"rs232c", 0},
    {"beep", 0},
    {"dummy", 0}
};

void device_on64(const char *name) {
    for (int i = 0; i < MAX_DEVICE64; i++) {
        if (device_table64[i].name && strcmp(device_table64[i].name, name) == 0) {
            device_table64[i].state = 1;
        }
    }
}

void device_off64(const char *name) {
    for (int i = 0; i < MAX_DEVICE64; i++) {
        if (device_table64[i].name && strcmp(device_table64[i].name, name) == 0) {
            device_table64[i].state = 0;
        }
    }
}

int device_request64(const char *name, int req, void *arg) {
    // ダミー: 常に成功
    return 0;
}

int get_device_state64(const char *name) {
    for (int i = 0; i < MAX_DEVICE64; i++) {
        if (device_table64[i].name && strcmp(device_table64[i].name, name) == 0) {
            return device_table64[i].state;
        }
    }
    return -1;
}

// --- ここまで追加 ---

// --- 本物のデバイスI/O・ドライバ雰囲気API ---

typedef int (*device_isr64_t)(void*);
typedef struct {
    const char *name;
    device_isr64_t isr;
    int irq;
} device_driver64_t;

#define MAX_DRIVER64 8
static device_driver64_t driver_table64[MAX_DRIVER64];
static int driver_count64 = 0;

int register_device_driver64(const char *name, device_isr64_t isr, int irq) {
    if (driver_count64 < MAX_DRIVER64) {
        driver_table64[driver_count64].name = name;
        driver_table64[driver_count64].isr = isr;
        driver_table64[driver_count64].irq = irq;
        driver_count64++;
        return 0;
    }
    return -1;
}

int handle_device_irq64(int irq, void *arg) {
    for (int i = 0; i < driver_count64; i++) {
        if (driver_table64[i].irq == irq && driver_table64[i].isr) {
            return driver_table64[i].isr(arg);
        }
    }
    return -1;
}

// IOポートアクセス雰囲気
unsigned char inb64(unsigned short port) { return 0; }
void outb64(unsigned short port, unsigned char val) { (void)port; (void)val; }

// --- ここまで追加 ---
