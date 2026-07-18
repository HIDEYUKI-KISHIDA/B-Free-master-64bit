#include <stdio.h>
#include "procfs.c"

// カーネル情報を/procに公開する雛形

static void procfs_kernel_init(void) {
    procfs_register("/proc/version", "B-Free x86_64 kernel 0.1");
    procfs_register("/proc/cpuinfo", "x86_64, 1core, MMU:yes");
    procfs_register("/proc/meminfo", "RAM: 256MB");
    // 必要に応じて拡張
}

// カーネル初期化時に呼び出すこと
