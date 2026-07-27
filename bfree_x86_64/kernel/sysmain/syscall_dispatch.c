#include "syscall_dispatch.h"
#include <string.h>

#define BFREE_NR_SYSCALLS 512

static int bfree_syscall_implemented[BFREE_NR_SYSCALLS];

void bfree_syscall_registry_init(void)
{
    memset(bfree_syscall_implemented, 0, sizeof(bfree_syscall_implemented));
    /* M17 ABI holes categories 0-3 */
    bfree_syscall_implemented[0] = 1;  /* read */
    bfree_syscall_implemented[1] = 1;  /* write */
    bfree_syscall_implemented[2] = 1;  /* open */
    bfree_syscall_implemented[3] = 1;  /* close */
    bfree_syscall_implemented[4] = 1;  /* stat */
    bfree_syscall_implemented[5] = 1;  /* fstat */
    bfree_syscall_implemented[6] = 1;  /* lstat */
    bfree_syscall_implemented[7] = 1;  /* poll */
    bfree_syscall_implemented[8] = 1;  /* lseek */
    bfree_syscall_implemented[9] = 1;  /* mmap */
    bfree_syscall_implemented[10] = 1;  /* mprotect */
    bfree_syscall_implemented[11] = 1;  /* munmap */
    bfree_syscall_implemented[12] = 1;  /* brk */
    bfree_syscall_implemented[13] = 1;  /* rt_sigaction */
    bfree_syscall_implemented[14] = 1;  /* rt_sigprocmask */
    bfree_syscall_implemented[15] = 1;  /* rt_sigreturn */
    bfree_syscall_implemented[16] = 1;  /* ioctl */
    bfree_syscall_implemented[17] = 1;  /* pread64 */
    bfree_syscall_implemented[18] = 1;  /* pwrite64 */
    bfree_syscall_implemented[19] = 1;  /* readv */
    bfree_syscall_implemented[20] = 1;  /* writev */
    bfree_syscall_implemented[21] = 1;  /* access */
    bfree_syscall_implemented[22] = 1;  /* pipe */
    bfree_syscall_implemented[23] = 1;  /* select */
    bfree_syscall_implemented[24] = 1;  /* sched_yield */
    bfree_syscall_implemented[25] = 1;  /* mremap */
    bfree_syscall_implemented[26] = 1;  /* msync */
    bfree_syscall_implemented[28] = 1;  /* madvise */
    bfree_syscall_implemented[29] = 1;  /* shmget */
    bfree_syscall_implemented[30] = 1;  /* shmat */
    bfree_syscall_implemented[31] = 1;  /* shmctl */
    bfree_syscall_implemented[32] = 1;  /* dup */
    bfree_syscall_implemented[33] = 1;  /* dup2 */
    bfree_syscall_implemented[34] = 1;  /* pause */
    bfree_syscall_implemented[35] = 1;  /* nanosleep */
    bfree_syscall_implemented[36] = 1;  /* getitimer */
    bfree_syscall_implemented[37] = 1;  /* alarm */
    bfree_syscall_implemented[38] = 1;  /* setitimer */
    bfree_syscall_implemented[39] = 1;  /* getpid */
    bfree_syscall_implemented[40] = 1;  /* sendfile */
    bfree_syscall_implemented[41] = 1;  /* socket */
    bfree_syscall_implemented[42] = 1;  /* connect */
    bfree_syscall_implemented[43] = 1;  /* accept */
    bfree_syscall_implemented[44] = 1;  /* sendto */
    bfree_syscall_implemented[45] = 1;  /* recvfrom */
    bfree_syscall_implemented[46] = 1;  /* sendmsg */
    bfree_syscall_implemented[47] = 1;  /* recvmsg */
    bfree_syscall_implemented[48] = 1;  /* shutdown */
    bfree_syscall_implemented[49] = 1;  /* bind */
    bfree_syscall_implemented[50] = 1;  /* listen */
    bfree_syscall_implemented[51] = 1;  /* getsockname */
    bfree_syscall_implemented[52] = 1;  /* getpeername */
    bfree_syscall_implemented[53] = 1;  /* socketpair */
    bfree_syscall_implemented[54] = 1;  /* setsockopt */
    bfree_syscall_implemented[55] = 1;  /* getsockopt */
    bfree_syscall_implemented[56] = 1;  /* clone */
    bfree_syscall_implemented[57] = 1;  /* fork */
    bfree_syscall_implemented[58] = 1;  /* vfork */
    bfree_syscall_implemented[59] = 1;  /* execve */
    bfree_syscall_implemented[60] = 1;  /* exit */
    bfree_syscall_implemented[61] = 1;  /* wait4 */
    bfree_syscall_implemented[62] = 1;  /* kill */
    bfree_syscall_implemented[63] = 1;  /* uname */
    bfree_syscall_implemented[64] = 1;  /* semget */
    bfree_syscall_implemented[65] = 1;  /* semop */
    bfree_syscall_implemented[66] = 1;  /* semctl */
    bfree_syscall_implemented[67] = 1;  /* shmdt */
    bfree_syscall_implemented[68] = 1;  /* msgget */
    bfree_syscall_implemented[69] = 1;  /* msgsnd */
    bfree_syscall_implemented[70] = 1;  /* msgrcv */
    bfree_syscall_implemented[71] = 1;  /* msgctl */
    bfree_syscall_implemented[72] = 1;  /* fcntl */
    bfree_syscall_implemented[73] = 1;  /* flock */
    bfree_syscall_implemented[74] = 1;  /* fsync */
    bfree_syscall_implemented[75] = 1;  /* fdatasync */
    bfree_syscall_implemented[76] = 1;  /* truncate */
    bfree_syscall_implemented[77] = 1;  /* ftruncate */
    bfree_syscall_implemented[79] = 1;  /* getcwd */
    bfree_syscall_implemented[80] = 1;  /* chdir */
    bfree_syscall_implemented[81] = 1;  /* fchdir */
    bfree_syscall_implemented[82] = 1;  /* rename */
    bfree_syscall_implemented[83] = 1;  /* mkdir */
    bfree_syscall_implemented[84] = 1;  /* rmdir */
    bfree_syscall_implemented[85] = 1;  /* creat */
    bfree_syscall_implemented[86] = 1;  /* link */
    bfree_syscall_implemented[87] = 1;  /* unlink */
    bfree_syscall_implemented[88] = 1;  /* symlink */
    bfree_syscall_implemented[89] = 1;  /* readlink */
    bfree_syscall_implemented[90] = 1;  /* chmod */
    bfree_syscall_implemented[91] = 1;  /* fchmod */
    bfree_syscall_implemented[92] = 1;  /* chown */
    bfree_syscall_implemented[93] = 1;  /* fchown */
    bfree_syscall_implemented[94] = 1;  /* lchown */
    bfree_syscall_implemented[95] = 1;  /* umask */
    bfree_syscall_implemented[96] = 1;  /* gettimeofday */
    bfree_syscall_implemented[97] = 1;  /* getrlimit */
    bfree_syscall_implemented[98] = 1;  /* getrusage */
    bfree_syscall_implemented[99] = 1;  /* sysinfo */
    bfree_syscall_implemented[100] = 1;  /* times */
    bfree_syscall_implemented[102] = 1;  /* getuid */
    bfree_syscall_implemented[104] = 1;  /* getgid */
    bfree_syscall_implemented[105] = 1;  /* setuid */
    bfree_syscall_implemented[106] = 1;  /* setgid */
    bfree_syscall_implemented[107] = 1;  /* geteuid */
    bfree_syscall_implemented[108] = 1;  /* getegid */
    bfree_syscall_implemented[109] = 1;  /* setpgid */
    bfree_syscall_implemented[110] = 1;  /* getppid */
    bfree_syscall_implemented[111] = 1;  /* getpgrp */
    bfree_syscall_implemented[112] = 1;  /* setsid */
    bfree_syscall_implemented[113] = 1;  /* setreuid */
    bfree_syscall_implemented[114] = 1;  /* setregid */
    bfree_syscall_implemented[115] = 1;  /* getgroups */
    bfree_syscall_implemented[116] = 1;  /* setgroups */
    bfree_syscall_implemented[117] = 1;  /* setresuid */
    bfree_syscall_implemented[118] = 1;  /* getresuid */
    bfree_syscall_implemented[119] = 1;  /* setresgid */
    bfree_syscall_implemented[120] = 1;  /* getresgid */
    bfree_syscall_implemented[121] = 1;  /* getpgid */
    bfree_syscall_implemented[124] = 1;  /* getsid */
    bfree_syscall_implemented[125] = 1;  /* capget */
    bfree_syscall_implemented[126] = 1;  /* capset */
    bfree_syscall_implemented[132] = 1;  /* utime */
    bfree_syscall_implemented[133] = 1;  /* mknod */
    bfree_syscall_implemented[137] = 1;  /* statfs */
    bfree_syscall_implemented[138] = 1;  /* fstatfs */
    bfree_syscall_implemented[140] = 1;  /* getpriority */
    bfree_syscall_implemented[141] = 1;  /* setpriority */
    bfree_syscall_implemented[157] = 1;  /* prctl */
    bfree_syscall_implemented[158] = 1;  /* arch_prctl */
    bfree_syscall_implemented[160] = 1;  /* setrlimit */
    bfree_syscall_implemented[161] = 1;  /* chroot */
    bfree_syscall_implemented[162] = 1;  /* sync */
    bfree_syscall_implemented[165] = 1;  /* mount */
    bfree_syscall_implemented[166] = 1;  /* umount2 */
    bfree_syscall_implemented[186] = 1;  /* gettid */
    bfree_syscall_implemented[200] = 1;  /* tkill */
    bfree_syscall_implemented[201] = 1;  /* time */
    bfree_syscall_implemented[202] = 1;  /* futex */
    bfree_syscall_implemented[213] = 1;  /* epoll_create */
    bfree_syscall_implemented[217] = 1;  /* getdents64 */
    bfree_syscall_implemented[218] = 1;  /* set_tid_address */
    bfree_syscall_implemented[228] = 1;  /* clock_gettime */
    bfree_syscall_implemented[229] = 1;  /* clock_getres */
    bfree_syscall_implemented[230] = 1;  /* clock_nanosleep */
    bfree_syscall_implemented[231] = 1;  /* exit_group */
    bfree_syscall_implemented[232] = 1;  /* epoll_wait */
    bfree_syscall_implemented[233] = 1;  /* epoll_ctl */
    bfree_syscall_implemented[234] = 1;  /* tgkill */
    bfree_syscall_implemented[235] = 1;  /* utimes */
    bfree_syscall_implemented[247] = 1;  /* waitid */
    bfree_syscall_implemented[253] = 1;  /* inotify_init */
    bfree_syscall_implemented[254] = 1;  /* inotify_add_watch */
    bfree_syscall_implemented[255] = 1;  /* inotify_rm_watch */
    bfree_syscall_implemented[257] = 1;  /* openat */
    bfree_syscall_implemented[258] = 1;  /* mkdirat */
    bfree_syscall_implemented[259] = 1;  /* mknodat */
    bfree_syscall_implemented[260] = 1;  /* fchownat */
    bfree_syscall_implemented[262] = 1;  /* newfstatat */
    bfree_syscall_implemented[263] = 1;  /* unlinkat */
    bfree_syscall_implemented[264] = 1;  /* renameat */
    bfree_syscall_implemented[265] = 1;  /* linkat */
    bfree_syscall_implemented[266] = 1;  /* symlinkat */
    bfree_syscall_implemented[267] = 1;  /* readlinkat */
    bfree_syscall_implemented[268] = 1;  /* fchmodat */
    bfree_syscall_implemented[269] = 1;  /* faccessat */
    bfree_syscall_implemented[270] = 1;  /* pselect6 */
    bfree_syscall_implemented[271] = 1;  /* ppoll */
    bfree_syscall_implemented[272] = 1;  /* unshare */
    bfree_syscall_implemented[273] = 1;  /* set_robust_list */
    bfree_syscall_implemented[280] = 1;  /* utimensat */
    bfree_syscall_implemented[281] = 1;  /* epoll_pwait */
    bfree_syscall_implemented[283] = 1;  /* timerfd_create */
    bfree_syscall_implemented[284] = 1;  /* eventfd */
    bfree_syscall_implemented[285] = 1;  /* fallocate */
    bfree_syscall_implemented[288] = 1;  /* accept4 */
    bfree_syscall_implemented[289] = 1;  /* signalfd4 */
    bfree_syscall_implemented[290] = 1;  /* eventfd2 */
    bfree_syscall_implemented[291] = 1;  /* epoll_create1 */
    bfree_syscall_implemented[292] = 1;  /* dup3 */
    bfree_syscall_implemented[293] = 1;  /* pipe2 */
    bfree_syscall_implemented[294] = 1;  /* inotify_init1 */
    bfree_syscall_implemented[295] = 1;  /* preadv */
    bfree_syscall_implemented[296] = 1;  /* pwritev */
    bfree_syscall_implemented[302] = 1;  /* prlimit64 */
    bfree_syscall_implemented[309] = 1;  /* getcpu */
    bfree_syscall_implemented[316] = 1;  /* renameat2 */
    bfree_syscall_implemented[318] = 1;  /* getrandom */
    bfree_syscall_implemented[319] = 1;  /* memfd_create */
    bfree_syscall_implemented[322] = 1;  /* execveat */
    bfree_syscall_implemented[332] = 1;  /* statx */
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

