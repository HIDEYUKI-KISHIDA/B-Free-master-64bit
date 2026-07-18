#include <stdio.h>
#include "devmgmt.c"

// 高度なデバイス管理UI/CLI（lsdev, devctl, sysctl等）雛形

void lsdev(void) {
    printf("device_id\tstate\tpriority\tdependency\n");
    for (int i = 0; i < devmgmt_count; ++i) {
        printf("%u\t%d\t%d\t%d\n", devmgmt_table[i].device_id, devmgmt_table[i].state, devmgmt_table[i].priority, devmgmt_table[i].dependency);
    }
}

// 今後: devctl, sysctl等のコマンド拡張
