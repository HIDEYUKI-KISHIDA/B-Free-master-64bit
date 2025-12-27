// posix64.c - 64ビット用 POSIX風API雛形（ダミー実装）
#include "../include64/types.h"

// fork/exec/wait/pipe/dup等の雰囲気API

int posix_fork64(void) {
    // 本来はプロセス複製
    return 0;
}

int posix_execve64(const char *path, char *const argv[], char *const envp[]) {
    // 本来はバイナリロード
    return 0;
}

int posix_wait64(int *status) {
    // 本来は子プロセス終了待ち
    return 0;
}

int posix_pipe64(int fds[2]) {
    fds[0] = 0; fds[1] = 1;
    return 0;
}

int posix_dup64(int oldfd) {
    return oldfd;
}

int posix_dup2_64(int oldfd, int newfd) {
    return newfd;
}

int posix_chdir64(const char *path) {
    return 0;
}

int posix_getcwd64(char *buf, int size) {
    if (size > 0) buf[0] = '/';
    if (size > 1) buf[1] = 0;
    return 0;
}
