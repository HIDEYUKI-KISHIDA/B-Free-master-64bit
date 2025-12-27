// sysinfo64.c - 64ビット用 システム情報API雛形（ダミー実装）
#include "../include64/types.h"

struct utsname64 {
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[32];
    char machine[32];
};

int uname64(struct utsname64 *buf) {
    if (!buf) return -1;
    strncpy(buf->sysname, "B-Free64", 31);
    strncpy(buf->nodename, "bfree64node", 31);
    strncpy(buf->release, "0.1", 31);
    strncpy(buf->version, "2025-12-27", 31);
    strncpy(buf->machine, "x86_64", 31);
    return 0;
}

struct sysinfo64 {
    unsigned long uptime;
    unsigned long totalram;
    unsigned long freeram;
};

int sysinfo64(struct sysinfo64 *info) {
    if (!info) return -1;
    info->uptime = 12345;
    info->totalram = 64 * 1024 * 1024;
    info->freeram = 32 * 1024 * 1024;
    return 0;
}

int gettimeofday64(void *tv, void *tz) { return 0; }
int clock_gettime64(int clk_id, void *tp) { return 0; }
