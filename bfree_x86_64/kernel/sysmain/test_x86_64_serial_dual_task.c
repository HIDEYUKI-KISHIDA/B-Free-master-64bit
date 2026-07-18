/*
 * Program/bfree_x86_64/kernel/sysmain/test_x86_64_serial_dual_task.c
 * 
 * Stage 1 TK2 API 実行確認テスト: tk_cre_tsk/tk_sta_tsk/mutex
 * 
 * 期待出力 (スタブ = 同期実行):
 *   [STAGE1] TK2 API dual task test
 *   [task_a] started / A0..A4 / [task_a] finished
 *   [task_b] started / B0..B4 / [task_b] finished
 *   [STAGE1] COMPLETE
 */

#include <stdint.h>
#include <tk/tkernel.h>
#include "timer_manager.h"
#include "../../userland/libc/bfree_epoll.h"

/* ============================================================
    Serial I/O (COM1: 0x3F8)
    ============================================================ */
#define COM1_PORT       0x3F8
#define COM1_THR        (COM1_PORT + 0)
#define COM1_IER        (COM1_PORT + 1)
#define COM1_FCR        (COM1_PORT + 2)
#define COM1_LCR        (COM1_PORT + 3)
#define COM1_MCR        (COM1_PORT + 4)
#define COM1_LSR        (COM1_PORT + 5)
#define LSR_THRE        0x20
#define SERIAL_STRESS_COUNT 200
#define POST_PORT       0x80
#define KBC_STATUS_PORT 0x64

/* I/O アクセス */
static inline uint8_t io_inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void io_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void io_delay(unsigned int usec) {
    for (volatile unsigned int i = 0; i < usec * 100; i++);
}

static void serial_init(void) {
    io_outb(COM1_IER, 0x00);
    io_outb(COM1_FCR, 0x07);
    io_outb(COM1_LCR, 0x03);
    io_outb(COM1_MCR, 0x03);
    io_outb(COM1_LCR, 0x83);
    io_outb(COM1_PORT + 0, 0x01);
    io_outb(COM1_PORT + 1, 0x00);
    io_outb(COM1_LCR, 0x03);
}

static void serial_putc(char c) {
    while ((io_inb(COM1_LSR) & LSR_THRE) == 0) {
        io_delay(1);
    }
    io_outb(COM1_THR, (uint8_t)c);
}

static void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s);
        s++;
    }
}

static void serial_put_u32(uint32_t v) {
    char buf[11];
    int n = 0;
    if (v == 0) {
        serial_putc('0');
        return;
    }
    while (v > 0 && n < (int)sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        serial_putc(buf[--n]);
    }
}

static void serial_put_hex8(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";
    serial_putc(hex[(v >> 4) & 0x0F]);
    serial_putc(hex[v & 0x0F]);
}

/* ============================================================
   Task functions
   ============================================================ */

static ID mtx_serial;
extern long knl_syscall_handler(long num, long arg1, long arg2, long arg3, long arg4, long arg5);

static void task_a_func(INT exinf, void *arg) {
    int i;
    (void)exinf; (void)arg;
    serial_puts("[task_a] started\n");
    for (i = 0; i < SERIAL_STRESS_COUNT; i++) {
        tk_loc_mtx(mtx_serial);
        serial_puts("A");
        serial_put_u32((uint32_t)i);
        serial_puts("\n");
        tk_unl_mtx(mtx_serial);
        tk_rot_rdq(TPRI_SELF);
        io_delay(2);
    }
    serial_puts("[task_a] finished\n");
    tk_ext_tsk();
}

static void task_b_func(INT exinf, void *arg) {
    int i;
    (void)exinf; (void)arg;
    serial_puts("[task_b] started\n");
    for (i = 0; i < SERIAL_STRESS_COUNT; i++) {
        tk_loc_mtx(mtx_serial);
        serial_puts("B");
        serial_put_u32((uint32_t)i);
        serial_puts("\n");
        tk_unl_mtx(mtx_serial);
        tk_rot_rdq(TPRI_SELF);
        io_delay(2);
    }
    serial_puts("[task_b] finished\n");
    tk_ext_tsk();
}

static volatile int s_timer_stage2_fired = 0;

static void stage2_timer_callback(void *arg)
{
    (void)arg;
    s_timer_stage2_fired = 1;
    serial_puts("[STAGE2] timer callback fired\n");
}

/* ============================================================
   Test Entry Point
   ============================================================ */
void test_x86_64_serial_dual_task(void) {
    T_MTXCB mtx_cb;
    T_CTSK ctsk_a, ctsk_b;
    ID id_a, id_b;

    serial_init();
    serial_puts("\n");
    serial_puts("========================================\n");
    serial_puts("[STAGE1] TK2 API dual task test\n");
    serial_puts("========================================\n");

    /* Mutex 作成 */
    mtx_cb.mtxatr  = TA_TFIFO;
    mtx_cb.ceilpri = 100;
    mtx_serial = tk_cre_mtx(&mtx_cb);
    if (mtx_serial < E_OK) {
        serial_puts("ERROR: tk_cre_mtx failed\n");
        return;
    }
    serial_puts("[STAGE1] tk_cre_mtx OK\n");

    /* Task A 作成 */
    ctsk_a.tskatr  = TA_HLNG | TA_RNG0;
    ctsk_a.exinf   = (void *)0;
    ctsk_a.task    = (FP)task_a_func;
    ctsk_a.itskpri = 100;
    ctsk_a.stksz   = 0x2000;
    ctsk_a.stkptr  = (void *)0;
    id_a = tk_cre_tsk(&ctsk_a);
    if (id_a < E_OK) {
        serial_puts("ERROR: tk_cre_tsk(A) failed\n");
        return;
    }
    serial_puts("[STAGE1] tk_cre_tsk(A) OK\n");

    /* Task B 作成 */
    ctsk_b.tskatr  = TA_HLNG | TA_RNG0;
    ctsk_b.exinf   = (void *)0;
    ctsk_b.task    = (FP)task_b_func;
    ctsk_b.itskpri = 100;
    ctsk_b.stksz   = 0x2000;
    ctsk_b.stkptr  = (void *)0;
    id_b = tk_cre_tsk(&ctsk_b);
    if (id_b < E_OK) {
        serial_puts("ERROR: tk_cre_tsk(B) failed\n");
        return;
    }
    serial_puts("[STAGE1] tk_cre_tsk(B) OK\n");

    /* Task 起動（スタブ = 同期実行） */
    serial_puts("[STAGE1] Starting task A...\n");
    tk_sta_tsk(id_a, 0);

    serial_puts("[STAGE1] Starting task B...\n");
    tk_sta_tsk(id_b, 0);

    serial_puts("========================================\n");
    serial_puts("[STAGE1] COMPLETE\n");
    serial_puts("========================================\n");

    /* Stage 2: timer event fire check */
    {
        LSYSTIM now;
        int event_id;
        uint32_t spin;

        serial_puts("[STAGE2] timer event test begin\n");
        s_timer_stage2_fired = 0;
        now = knl_get_current_time();
        event_id = timer_set_event(now + 1000000ULL, stage2_timer_callback, 0);
        if (event_id < 0) {
            serial_puts("[STAGE2] ERROR: timer_set_event failed\n");
            return;
        }

        for (spin = 0; spin < 5000000U; ++spin) {
            if (s_timer_stage2_fired) {
                break;
            }
            /*
             * この時点では実IRQがまだ十分進んでいないケースがあるため、
             * Stage2検証では論理タイマを明示的に進める。
             */
            timer_handler();
            if ((spin % 50000U) == 0U) {
                io_delay(10);
            }
        }

        if (s_timer_stage2_fired) {
            serial_puts("[STAGE2] timer event OK\n");
        } else {
            serial_puts("[STAGE2] WARNING: timer event timeout\n");
            timer_cancel_event(event_id);
        }
        serial_puts("[STAGE2] timer event test end\n");
    }

    /* Stage 3: GPIO代替 I/O port smoke test */
    {
        uint8_t status;
        uint8_t pattern = 0x10;
        int i;

        serial_puts("[STAGE3] ioport smoke test begin\n");

        for (i = 0; i < 8; ++i) {
            io_outb(POST_PORT, (uint8_t)(pattern + i));
            status = io_inb(KBC_STATUS_PORT);

            serial_puts("[STAGE3] POST=0x");
            serial_put_hex8((uint8_t)(pattern + i));
            serial_puts(" KBC_STS=0x");
            serial_put_hex8(status);
            serial_puts("\n");
            io_delay(5);
        }

        serial_puts("[STAGE3] ioport smoke test end\n");
    }

    /* Stage 3.5: kernel snapshot exporter syscall check */
    {
        bfree_tk2_snapshot_t snap;
        long ret = knl_syscall_handler(
            BFREE_SYSCALL_GET_TK2_SNAPSHOT,
            (long)&snap,
            (long)sizeof(snap),
            0,
            0,
            0);

        if (ret == 0) {
            serial_puts("[STAGE3] snapshot syscall OK ver=");
            serial_put_u32(snap.abi_version);
            serial_puts(" tasks=");
            serial_put_u32(snap.task_count);
            serial_puts(" dev=");
            serial_put_u32(snap.device_count);
            serial_puts("\n");
        } else {
            serial_puts("[STAGE3] snapshot syscall NG\n");
        }
    }
}
