static unsigned g_guest_futex_log_count;

/*
 * Linux 202: futex — single-waiter guest (H20 partial↑).
 * - Timed WAIT: poll deadline (sti+hlt) → -ETIMEDOUT (-110); leave *uaddr.
 * - Untimed WAIT: equality check → brief coop yield → short spin watching a
 *   single parked waiter / *uaddr change → clear *uaddr for Qt (must not hang
 *   phase3_pty_smoke). No multi-waiter queue.
 * - WAKE: clears matching parked slot (returns 1); soft-1 if none.
 */
static volatile int *g_futex_waiter_uaddr;
static int g_futex_waiter_val;
static int g_futex_waiter_armed;
static int g_futex_waiter_side;

long sys_futex(long uaddr, long op, long val, long timeout_ptr, long uaddr2, long val3)
{
    int cmd = (int)(op & 0x7f);

    (void)uaddr2;
    (void)val3;

    switch (cmd) {
    case 0: /* FUTEX_WAIT */
    case 9: /* FUTEX_WAIT_BITSET */
        if (uaddr == 0 || !bfree_user_vaddr_mapped((uint64_t)(uintptr_t)uaddr)) {
            return -14; /* EFAULT */
        }
        if (*(volatile int *)(uintptr_t)uaddr != (int)val) {
            return 0;
        }
        if (timeout_ptr != 0) {
            struct timespec *ts = (struct timespec *)(uintptr_t)timeout_ptr;
            uint64_t wait_us;
            uint64_t start;

            if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)ts)) {
                return -14;
            }
            wait_us = bfree_timespec_to_us(ts);
            start = knl_get_current_time();
            /* Allow timer IRQ so knl_get_current_time advances (SYSCALL clears IF). */
            __asm__ volatile("sti" ::: "memory");
            while ((knl_get_current_time() - start) < wait_us) {
                if (*(volatile int *)(uintptr_t)uaddr != (int)val) {
                    __asm__ volatile("cli" ::: "memory");
                    if (g_guest_futex_log_count < 8U) {
                        ++g_guest_futex_log_count;
                        uart_puts("[FUTEX] wait woken\n");
                    }
                    return 0;
                }
                __asm__ volatile("hlt" ::: "memory");
            }
            __asm__ volatile("cli" ::: "memory");
            /* Timed out: leave *uaddr unchanged (must not clear). */
            if (g_guest_futex_log_count < 8U) {
                ++g_guest_futex_log_count;
                uart_puts("[FUTEX] wait ETIMEDOUT\n");
            }
            return -110; /* ETIMEDOUT */
        }
        /*
         * Untimed WAIT: yield once if fork slice due, arm single waiter, brief
         * spin for WAKE, then clear *uaddr for Qt single-thread compatibility.
         */
        {
            long yr = bfree_guest_sched_maybe_yield();
            if (yr != 0) {
                return yr;
            }
        }
        if (*(volatile int *)(uintptr_t)uaddr != (int)val) {
            return 0;
        }
        g_futex_waiter_uaddr = (volatile int *)(uintptr_t)uaddr;
        g_futex_waiter_val = (int)val;
        g_futex_waiter_armed = 1;
        g_futex_waiter_side = g_coop_side;
        {
            int spins;
            __asm__ volatile("sti" ::: "memory");
            for (spins = 0; spins < 8; ++spins) {
                if (!g_futex_waiter_armed ||
                    *(volatile int *)(uintptr_t)uaddr != (int)val) {
                    break;
                }
                {
                    long yr = bfree_guest_sched_maybe_yield();
                    if (yr != 0) {
                        __asm__ volatile("cli" ::: "memory");
                        g_futex_waiter_armed = 0;
                        g_futex_waiter_uaddr = 0;
                        return yr;
                    }
                }
                __asm__ volatile("pause" ::: "memory");
            }
            __asm__ volatile("cli" ::: "memory");
        }
        g_futex_waiter_armed = 0;
        g_futex_waiter_uaddr = 0;
        (void)g_futex_waiter_val;
        (void)g_futex_waiter_side;
        *(int *)(uintptr_t)uaddr = 0;
        if (g_guest_futex_log_count < 8U) {
            ++g_guest_futex_log_count;
            uart_puts("[FUTEX] wait\n");
        }
        return 0;
    case 1: /* FUTEX_WAKE */
    case 10: /* FUTEX_WAKE_BITSET */
        if ((int)val <= 0) {
            return 0;
        }
        if (g_futex_waiter_armed &&
            g_futex_waiter_uaddr == (volatile int *)(uintptr_t)uaddr) {
            g_futex_waiter_armed = 0;
            g_futex_waiter_uaddr = 0;
            return 1;
        }
        return (cmd == 1) ? 1 : 0; /* soft-1 for plain WAKE without waiter */
    case 3: /* FUTEX_REQUEUE */
    case 4: /* FUTEX_CMP_REQUEUE */
    case 5: /* FUTEX_WAKE_OP */
        return 0;
    default:
        return -22; /* EINVAL */
    }
}