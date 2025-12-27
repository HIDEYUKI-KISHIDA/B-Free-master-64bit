// prof64.c - 64ビット用 プロファイラ/リソース監視API雛形（ダミー実装）
#include "../include64/types.h"

static unsigned long prof_cpu_ticks = 0;
static unsigned long prof_mem_used = 0;

void prof_tick64(void) {
    prof_cpu_ticks++;
}

void prof_set_mem64(unsigned long used) {
    prof_mem_used = used;
}

unsigned long prof_get_cpu64(void) {
    return prof_cpu_ticks;
}

unsigned long prof_get_mem64(void) {
    return prof_mem_used;
}
