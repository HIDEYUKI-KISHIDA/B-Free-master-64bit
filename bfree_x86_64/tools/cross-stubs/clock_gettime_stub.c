/* Link stub for meson has_function('clock_gettime') during cross configure. */
#include "time.h"

int clock_gettime(int clock_id, struct timespec *tp)
{
    if (tp) {
        tp->tv_sec = 0;
        tp->tv_nsec = 0;
    }
    (void)clock_id;
    return 0;
}
