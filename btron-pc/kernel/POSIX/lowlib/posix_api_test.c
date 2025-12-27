#include <stdio.h>
#include <errno.h>

extern int psys_fstat();
extern int psys_brk();
extern int psys_chroot();
extern int psys_mknod();
extern int psys_stime();
extern int psys_sync();
extern int psys_alarm();
extern int psys_sigreturn();

int main() {
    int ret;
    printf("POSIX API 未実装関数テスト\n");

    ret = psys_fstat();
    printf("psys_fstat() = %d (errno=%d)\n", ret, ENOSYS);
    ret = psys_brk();
    printf("psys_brk() = %d (errno=%d)\n", ret, ENOSYS);
    ret = psys_chroot();
    printf("psys_chroot() = %d (errno=%d)\n", ret, ENOSYS);
    ret = psys_mknod();
    printf("psys_mknod() = %d (errno=%d)\n", ret, ENOSYS);
    ret = psys_stime();
    printf("psys_stime() = %d (errno=%d)\n", ret, ENOSYS);
    ret = psys_sync();
    printf("psys_sync() = %d (errno=%d)\n", ret, ENOSYS);
    ret = psys_alarm();
    printf("psys_alarm() = %d (errno=%d)\n", ret, ENOSYS);
    ret = psys_sigreturn();
    printf("psys_sigreturn() = %d (errno=%d)\n", ret, ENOSYS);

    return 0;
}
