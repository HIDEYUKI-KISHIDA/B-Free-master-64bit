#include "syscall_dispatch.h"
#include "mount.h"
#include "cred.h"
#include "net_unix.h"
#include "ipc_shm.h"
#include <string.h>

#define BFREE_NR_SYSCALLS 512

static int bfree_syscall_implemented[BFREE_NR_SYSCALLS];

void bfree_syscall_registry_init(void)
{
    memset(bfree_syscall_implemented, 0, sizeof(bfree_syscall_implemented));
    /* M1-M4 implemented */
    bfree_syscall_implemented[0] = 1;   /* read */
    bfree_syscall_implemented[1] = 1;   /* write */
    bfree_syscall_implemented[2] = 1;   /* open */
    bfree_syscall_implemented[3] = 1;   /* close */
    bfree_syscall_implemented[4] = 1;   /* stat */
    bfree_syscall_implemented[5] = 1;   /* fstat */
    bfree_syscall_implemented[7] = 1;   /* poll */
    bfree_syscall_implemented[8] = 1;   /* lseek */
    bfree_syscall_implemented[32] = 1;  /* dup */
    bfree_syscall_implemented[33] = 1;  /* dup2 */
    bfree_syscall_implemented[72] = 1;  /* fcntl */
    bfree_syscall_implemented[57] = 1;  /* fork */
    bfree_syscall_implemented[58] = 1;  /* vfork */
    bfree_syscall_implemented[59] = 1;  /* execve */
    bfree_syscall_implemented[60] = 1;  /* exit */
    bfree_syscall_implemented[61] = 1;  /* wait4 */
    bfree_syscall_implemented[247] = 1; /* waitid */
    bfree_syscall_implemented[22] = 1;  /* pipe */
    bfree_syscall_implemented[12] = 1;  /* brk */
    bfree_syscall_implemented[9] = 1;   /* mmap */
    bfree_syscall_implemented[56] = 1;  /* clone */
    bfree_syscall_implemented[202] = 1; /* futex */
    bfree_syscall_implemented[157] = 1; /* prlimit64 */
    bfree_syscall_implemented[79] = 1;  /* getcwd */
    bfree_syscall_implemented[80] = 1;  /* chdir */
    bfree_syscall_implemented[62] = 1;  /* kill */
    bfree_syscall_implemented[13] = 1;  /* rt_sigaction */
    bfree_syscall_implemented[14] = 1;  /* rt_sigprocmask */
    bfree_syscall_implemented[15] = 1;  /* rt_sigreturn */
    bfree_syscall_implemented[16] = 1;  /* ioctl */
    bfree_syscall_implemented[39] = 1;  /* getpid */
    bfree_syscall_implemented[87] = 1;  /* unlink */
    bfree_syscall_implemented[90] = 1;  /* capget */
    bfree_syscall_implemented[91] = 1;  /* capset */
    bfree_syscall_implemented[109] = 1; /* setpgid */
    bfree_syscall_implemented[110] = 1; /* getppid */
    bfree_syscall_implemented[112] = 1; /* setsid */
    bfree_syscall_implemented[121] = 1; /* getpgid */
    bfree_syscall_implemented[217] = 1; /* getdents64 */
    bfree_syscall_implemented[257] = 1; /* openat */
    bfree_syscall_implemented[258] = 1; /* mkdirat */
    bfree_syscall_implemented[262] = 1; /* newfstatat */
    bfree_syscall_implemented[263] = 1; /* unlinkat */
    /* M5 */
    bfree_syscall_implemented[165] = 1; /* mount */
    bfree_syscall_implemented[166] = 1; /* umount2 */
    bfree_syscall_implemented[102] = 1; /* getuid */
    bfree_syscall_implemented[107] = 1; /* geteuid */
    bfree_syscall_implemented[104] = 1; /* getgid */
    bfree_syscall_implemented[108] = 1; /* getegid */
    bfree_syscall_implemented[105] = 1; /* setuid */
    bfree_syscall_implemented[106] = 1; /* setgid */
    bfree_syscall_implemented[113] = 1; /* setreuid */
    bfree_syscall_implemented[114] = 1; /* setregid */
    bfree_syscall_implemented[41] = 1;  /* socket */
    bfree_syscall_implemented[49] = 1;  /* bind */
    bfree_syscall_implemented[42] = 1;  /* connect */
    bfree_syscall_implemented[44] = 1;  /* sendto */
    bfree_syscall_implemented[45] = 1;  /* recvfrom */
    bfree_syscall_implemented[53] = 1;  /* socketpair */
    bfree_syscall_implemented[29] = 1;  /* shmget */
    bfree_syscall_implemented[30] = 1;  /* shmat */
    bfree_syscall_implemented[67] = 1;  /* shmdt */
    bfree_syscall_implemented[31] = 1;  /* shmctl */
}

int bfree_syscall_is_implemented(unsigned long nr)
{
    if (nr >= BFREE_NR_SYSCALLS)
        return 0;
    return bfree_syscall_implemented[nr];
}

int bfree_syscall_enosys_count(void)
{
    int count = 0;
    for (unsigned long nr = 0; nr < 400; nr++) {
        if (!bfree_syscall_is_implemented(nr))
            count++;
    }
    return count;
}
