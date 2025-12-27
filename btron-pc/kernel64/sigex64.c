// sigex64.c - 64ビット用 シグナル/例外拡張雛形（ダミー実装）
#include "../include64/types.h"

#define MAX_SIG64 32
static int sig_mask64 = 0;

int sigset64(int sig) {
    if (sig > 0 && sig < MAX_SIG64) sig_mask64 |= (1 << sig);
    return 0;
}

int sigclr64(int sig) {
    if (sig > 0 && sig < MAX_SIG64) sig_mask64 &= ~(1 << sig);
    return 0;
}

int sigismember64(int sig) {
    if (sig > 0 && sig < MAX_SIG64) return (sig_mask64 & (1 << sig)) ? 1 : 0;
    return 0;
}

int sigmask64(void) {
    return sig_mask64;
}
