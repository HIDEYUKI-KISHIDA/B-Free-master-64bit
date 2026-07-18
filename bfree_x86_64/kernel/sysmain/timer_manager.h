#ifndef TIMER_MANAGER_H
#define TIMER_MANAGER_H
#include <stdint.h>

typedef uint64_t LSYSTIM;

typedef void (*timer_callback_t)(void *);

int timer_set_event(LSYSTIM expire_time, timer_callback_t callback, void *arg);
void timer_cancel_event(int id);
void timer_purge_all(void);
void timer_handler(void);
void timer_process_events(void);
LSYSTIM knl_get_current_time(void);

#endif // TIMER_MANAGER_H
