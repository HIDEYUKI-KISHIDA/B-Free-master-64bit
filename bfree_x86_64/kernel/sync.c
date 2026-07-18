#include <stdint.h>

// マルチプロセス/マルチスレッド環境での排他制御・同期雛形

typedef struct {
    volatile int locked;
} spinlock_t;

void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
}

void spinlock_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1)) {}
}

void spinlock_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->locked);
}

// 今後: セマフォ・ミューテックス・条件変数等も拡張
