/*
 * bfree_subsystem などホスト向けリンクで、fbdev / syscall が参照するが
 * フルカーネルでは別 TU にあるシンボルを満たす最小スタブ。
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "gpu_backend.h"

void uart_puts(const char *s)
{
    (void)s;
}

void uart_puthex64(uint64_t val)
{
    (void)val;
}

static bfree_gpu_backend_state_t g_gpu_stub;

const bfree_gpu_backend_state_t *gpu_backend_state(void)
{
    return &g_gpu_stub;
}

void gpu_backend_init(void)
{
    memset(&g_gpu_stub, 0, sizeof(g_gpu_stub));
    g_gpu_stub.initialized = 1;
    g_gpu_stub.fbinfo.width = 64;
    g_gpu_stub.fbinfo.height = 64;
    g_gpu_stub.fbinfo.bpp = 32;
    g_gpu_stub.fbinfo.pitch = 64 * 4;
    g_gpu_stub.fbinfo.phys_addr = 0;
    g_gpu_stub.fbinfo.size = (size_t)64 * 64 * 4;
}

int gpu_backend_ioctl(int cmd, void *arg)
{
    if (cmd == TK2GPU_IOCTL_GET_INFO && arg != NULL) {
        memcpy(arg, &g_gpu_stub.fbinfo, sizeof(tk2gpu_fbinfo_t));
        return 0;
    }
    return TK2GPU_ENOSYS;
}
