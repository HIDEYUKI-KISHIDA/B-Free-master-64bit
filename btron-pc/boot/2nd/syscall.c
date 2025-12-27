/* システムコールディスパッチ雛形 */
#include "lib.h"

int syscall_dispatch(unsigned int *esp) {
    int syscall_num = esp[0]; // 仮: syscall番号はスタック先頭
    // 引数は esp[1] 以降
    switch (syscall_num) {
        case 1: // exit
            exit((int)esp[1]);
            return 0;
        case 2: // fork
            return sys_fork();
        case 3: // exec
            return sys_exec((int)esp[1], (void *)esp[2]);
        case 4: // wait
            return sys_wait((int)esp[1]);
        case 5: // read
            return vfs_read((void *)esp[1], (void *)esp[2], (int)esp[3]);
        case 6: // write
            return vfs_write((void *)esp[1], (void *)esp[2], (int)esp[3]);
        case 7: // open
            return (int)vfs_open((const char *)esp[1], (int)esp[2]);
        case 8: // close
            return vfs_close((void *)esp[1]);
        case 9: // stat
            return vfs_stat((const char *)esp[1], (void *)esp[2]);
        case 10: // time
            return (int)esp[1]; // 仮: RTC取得は別途
        // ...他のシステムコール...
        default:
            return -1; // 未定義
    }
}
