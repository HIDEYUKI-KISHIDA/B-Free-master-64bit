#ifndef BFREE_IPC_SHM_H
#define BFREE_IPC_SHM_H

#define BFREE_IPC_RMID 0
#define BFREE_IPC_STAT 2

struct bfree_shmid_ds {
    unsigned long shm_segsz;
    int shm_nattch;
};

int bfree_shmget(int key, unsigned long size, int shmflg);
void *bfree_shmat(int shmid, const void *shmaddr, int shmflg);
int bfree_shmdt(const void *shmaddr);
int bfree_shmctl(int shmid, int cmd, struct bfree_shmid_ds *buf);
void bfree_ipc_reset(void);

#endif
