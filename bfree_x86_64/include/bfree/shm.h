#ifndef BFREE_SHM_H
#define BFREE_SHM_H

#include <stddef.h>
#include <stdint.h>

#define BFREE_SHM_MAX_SEGMENTS 64

// 共有メモリセグメント管理構造体
typedef struct {
    int key;            // セグメントID
    size_t size;        // サイズ
    void *phys_addr;    // 物理アドレス
    int refcnt;         // 参照カウント
    int owner_pid;      // 所有プロセス
    int flags;          // アクセス権等
    int used;           // 使用中フラグ
} bfree_shmseg_t;


// システムコール番号（仮）
#define SYS_SHMGET   0x90
#define SYS_SHMAT    0x91
#define SYS_SHMDT    0x92
#define SYS_SHMCTL   0x93
#define SYS_SHMOPEN  0x94
#define SYS_SHMUNLINK 0x95
#define SYS_MMAP     0x96
#define SYS_MUNMAP   0x97
#define SYS_MSYNC    0x98
#define SYS_MPROTECT 0x99
#define SYS_MLOCK    0x9A
#define SYS_MUNLOCK  0x9B

// プロトタイプ
int sys_shmget(int key, size_t size, int shmflg);
void *sys_shmat(int shmid, void *shmaddr, int shmflg);
int sys_shmdt(void *shmaddr);
int sys_shmctl(int shmid, int cmd, void *buf);
int sys_shm_open(const char *name, int oflag, int mode);
int sys_shm_unlink(const char *name);
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int sys_munmap(void *addr, size_t length);
int sys_msync(void *addr, size_t length, int flags);
int sys_mprotect(void *addr, size_t len, int prot);
int sys_mlock(const void *addr, size_t len);
int sys_munlock(const void *addr, size_t len);

#endif // BFREE_SHM_H
