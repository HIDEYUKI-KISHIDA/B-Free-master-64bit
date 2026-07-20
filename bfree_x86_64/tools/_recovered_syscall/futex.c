/* Recovered H20 futex (waiter slots + timed wait). Replaces HEAD stub sys_futex.
 * Source: 97982f27 StrReplace.
 */

static unsigned g_guest_futex_log_count;

/*
 * Linux 202: futex — guest coop (H20).
 * - timeout_ptr set: poll until deadline (sti+hlt), return -ETIMEDOUT (-110)
 *   WITHOUT clearing *uaddr (word stays locked — correct for timed wait).
 * - no timeout: brief coop yield, then park on a small waiter queue until
 *   FUTEX_WAKE or clear *uaddr (Qt single-thread workaround).
 */
#define BFREE_FUTEX_WAITERS 4
static volatile int *g_futex_waiter_uaddr[BFREE_FUTEX_WAITERS];
static int g_futex_waiter_armed[BFREE_FUTEX_WAITERS];

static int bfree_futex_arm_waiter(volatile int *ua)
{
    int i;

    for (i = 0; i < BFREE_FUTEX_WAITERS; ++i) {
        if (!g_futex_waiter_armed[i]) {
            g_futex_waiter_uaddr[i] = ua;
            g_futex_waiter_armed[i] = 1;
            return i;
        }
    }
    return -1;
}

static void bfree_futex_disarm_slot(int slot)
{
    if (slot < 0 || slot >= BFREE_FUTEX_WAITERS) {
        return;
    }
    g_futex_waiter_armed[slot] = 0;
    g_futex_waiter_uaddr[slot] = 0;
}

static int bfree_futex_slot_woken(int slot, volatile int *ua, int val)
{
    if (slot < 0) {
        return *(volatile int *)ua != val;
    }
    return !g_futex_waiter_armed[slot] || *(volatile int *)ua != val;
}

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
         * Untimed WAIT: brief coop yield, then arm a waiter slot so
         * FUTEX_WAKE can unblock. Finally clear *uaddr for Qt single-thread.
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
        {
            int slot = bfree_futex_arm_waiter((volatile int *)(uintptr_t)uaddr);
            int spins;
            __asm__ volatile("sti" ::: "memory");
            for (spins = 0; spins < 8; ++spins) {
                if (bfree_futex_slot_woken(slot, (volatile int *)(uintptr_t)uaddr,
                                           (int)val)) {
                    break;
                }
                {
                    long yr = bfree_guest_sched_maybe_yield();
                    if (yr != 0) {
                        __asm__ volatile("cli" ::: "memory");
                        bfree_futex_disarm_slot(slot);
                        return yr;
                    }
                }
                __asm__ volatile("pause" ::: "memory");
            }
            __asm__ volatile("cli" ::: "memory");
            bfree_futex_disarm_slot(slot);
        }
        *(int *)(uintptr_t)uaddr = 0;
        if (g_guest_futex_log_count < 8U) {
            ++g_guest_futex_log_count;
            uart_puts("[FUTEX] wait\n");
        }
        return 0;
    case 1: /* FUTEX_WAKE */
        if ((int)val <= 0) {
            return 0;
        }
        {
            int want = (int)val;
            int woke = 0;
            int i;

            for (i = 0; i < BFREE_FUTEX_WAITERS && woke < want; ++i) {
                if (g_futex_waiter_armed[i] &&
                    g_futex_waiter_uaddr[i] == (volatile int *)(uintptr_t)uaddr) {
                    g_futex_waiter_armed[i] = 0;
                    g_futex_waiter_uaddr[i] = 0;
                    ++woke;
                }
            }
            return woke > 0 ? (long)woke : 1; /* soft success if no waiter */
        }
    case 3: /* FUTEX_REQUEUE */
    case 4: /* FUTEX_CMP_REQUEUE */
    case 5: /* FUTEX_WAKE_OP */
    case 10: /* FUTEX_WAKE_BITSET */
        return 0;
    default:
        return -22; /* EINVAL */
    }
}
