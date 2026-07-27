#include "ipc_shm.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#define BFREE_SHM_MAX 8
#define BFREE_SHM_SEG_SIZE 4096
#define BFREE_IPC_PRIVATE 0

struct bfree_shm_seg {
    int in_use;
    int key;
    int shmid;
    unsigned char data[BFREE_SHM_SEG_SIZE];
    int nattach;
};

static struct bfree_shm_seg bfree_shm[BFREE_SHM_MAX];
static int bfree_shm_next_id = 1;

static struct bfree_shm_seg *bfree_shm_find_key(int key)
{
    for (int i = 0; i < BFREE_SHM_MAX; i++) {
        if (bfree_shm[i].in_use && bfree_shm[i].key == key)
            return &bfree_shm[i];
    }
    return 0;
}

static struct bfree_shm_seg *bfree_shm_find_id(int shmid)
{
    for (int i = 0; i < BFREE_SHM_MAX; i++) {
        if (bfree_shm[i].in_use && bfree_shm[i].shmid == shmid)
            return &bfree_shm[i];
    }
    return 0;
}

int bfree_shmget(int key, unsigned long size, int shmflg)
{
    (void)shmflg;
    if (size > BFREE_SHM_SEG_SIZE)
        return -EINVAL;
    if (key != BFREE_IPC_PRIVATE) {
        struct bfree_shm_seg *existing = bfree_shm_find_key(key);
        if (existing)
            return existing->shmid;
    }
    for (int i = 0; i < BFREE_SHM_MAX; i++) {
        if (!bfree_shm[i].in_use) {
            memset(&bfree_shm[i], 0, sizeof(bfree_shm[i]));
            bfree_shm[i].in_use = 1;
            bfree_shm[i].key = key;
            bfree_shm[i].shmid = bfree_shm_next_id++;
            return bfree_shm[i].shmid;
        }
    }
    return -ENOSPC;
}

void *bfree_shmat(int shmid, const void *shmaddr, int shmflg)
{
    (void)shmaddr;
    (void)shmflg;
    struct bfree_shm_seg *seg = bfree_shm_find_id(shmid);
    if (!seg)
        return (void *)(intptr_t)-1;
    seg->nattach++;
    return seg->data;
}

int bfree_shmdt(const void *shmaddr)
{
    if (!shmaddr)
        return -EINVAL;
    for (int i = 0; i < BFREE_SHM_MAX; i++) {
        if (bfree_shm[i].in_use && (void *)bfree_shm[i].data == shmaddr) {
            if (bfree_shm[i].nattach > 0)
                bfree_shm[i].nattach--;
            return 0;
        }
    }
    return -EINVAL;
}

int bfree_shmctl(int shmid, int cmd, struct bfree_shmid_ds *buf)
{
    struct bfree_shm_seg *seg = bfree_shm_find_id(shmid);
    if (!seg)
        return -EINVAL;
    if (cmd == BFREE_IPC_RMID) {
        if (seg->nattach > 0)
            return -EBUSY;
        memset(seg, 0, sizeof(*seg));
        return 0;
    }
    if (cmd == BFREE_IPC_STAT && buf) {
        buf->shm_segsz = BFREE_SHM_SEG_SIZE;
        buf->shm_nattch = seg->nattach;
        return 0;
    }
    return -EINVAL;
}

void bfree_ipc_reset(void)
{
    memset(bfree_shm, 0, sizeof(bfree_shm));
    bfree_shm_next_id = 1;
}
