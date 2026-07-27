#include "ipc_sysv.h"

#include <errno.h>
#include <string.h>

#define BFREE_SEM_MAX 8
#define BFREE_MSG_MAX 8
#define BFREE_MSG_BUF 256

struct bfree_sem {
	int in_use;
	int key;
	int nsems;
	short vals[16];
};

struct bfree_msgq {
	int in_use;
	int key;
	unsigned char buf[BFREE_MSG_BUF];
	unsigned long len;
	long type;
};

static struct bfree_sem bfree_sems[BFREE_SEM_MAX];
static struct bfree_msgq bfree_msgs[BFREE_MSG_MAX];

void bfree_ipc_sysv_reset(void)
{
	memset(bfree_sems, 0, sizeof(bfree_sems));
	memset(bfree_msgs, 0, sizeof(bfree_msgs));
}

int bfree_semget(int key, int nsems, int semflg)
{
	int i;

	if (nsems <= 0 || nsems > 16)
		return -EINVAL;
	if (key != 0) {
		for (i = 0; i < BFREE_SEM_MAX; i++) {
			if (bfree_sems[i].in_use && bfree_sems[i].key == key)
				return i;
		}
	}
	if (!(semflg & BFREE_IPC_CREAT) && key != 0)
		return -ENOENT;
	for (i = 0; i < BFREE_SEM_MAX; i++) {
		if (!bfree_sems[i].in_use) {
			memset(&bfree_sems[i], 0, sizeof(bfree_sems[i]));
			bfree_sems[i].in_use = 1;
			bfree_sems[i].key = key;
			bfree_sems[i].nsems = nsems;
			return i;
		}
	}
	return -ENOSPC;
}

int bfree_semop(int semid, short *sops, unsigned nsops)
{
	unsigned i;

	if (semid < 0 || semid >= BFREE_SEM_MAX || !bfree_sems[semid].in_use)
		return -EINVAL;
	if (sops == NULL || nsops == 0)
		return -EINVAL;
	for (i = 0; i < nsops; i++) {
		int num = sops[i * 3];
		short op = sops[i * 3 + 1];

		if (num < 0 || num >= bfree_sems[semid].nsems)
			return -EFBIG;
		if (bfree_sems[semid].vals[num] + op < 0)
			return -EAGAIN;
		bfree_sems[semid].vals[num] = (short)(bfree_sems[semid].vals[num] + op);
	}
	return 0;
}

int bfree_semctl(int semid, int semnum, int cmd, void *arg)
{
	(void)arg;
	if (semid < 0 || semid >= BFREE_SEM_MAX || !bfree_sems[semid].in_use)
		return -EINVAL;
	if (cmd == BFREE_IPC_RMID) {
		bfree_sems[semid].in_use = 0;
		return 0;
	}
	if (cmd == 16 /* GETVAL */) {
		if (semnum < 0 || semnum >= bfree_sems[semid].nsems)
			return -EINVAL;
		return bfree_sems[semid].vals[semnum];
	}
	if (cmd == 17 /* SETVAL */) {
		if (semnum < 0 || semnum >= bfree_sems[semid].nsems)
			return -EINVAL;
		bfree_sems[semid].vals[semnum] = (short)(long)arg;
		return 0;
	}
	return -EINVAL;
}

int bfree_msgget(int key, int msgflg)
{
	int i;

	if (key != 0) {
		for (i = 0; i < BFREE_MSG_MAX; i++) {
			if (bfree_msgs[i].in_use && bfree_msgs[i].key == key)
				return i;
		}
	}
	if (!(msgflg & BFREE_IPC_CREAT) && key != 0)
		return -ENOENT;
	for (i = 0; i < BFREE_MSG_MAX; i++) {
		if (!bfree_msgs[i].in_use) {
			memset(&bfree_msgs[i], 0, sizeof(bfree_msgs[i]));
			bfree_msgs[i].in_use = 1;
			bfree_msgs[i].key = key;
			return i;
		}
	}
	return -ENOSPC;
}

int bfree_msgsnd(int msqid, const void *msgp, unsigned long msgsz, int msgflg)
{
	const long *hdr = msgp;

	(void)msgflg;
	if (msqid < 0 || msqid >= BFREE_MSG_MAX || !bfree_msgs[msqid].in_use)
		return -EINVAL;
	if (msgp == NULL || msgsz > BFREE_MSG_BUF - sizeof(long))
		return -EINVAL;
	if (bfree_msgs[msqid].len != 0)
		return -EAGAIN;
	bfree_msgs[msqid].type = hdr[0];
	memcpy(bfree_msgs[msqid].buf, (const char *)msgp + sizeof(long), msgsz);
	bfree_msgs[msqid].len = msgsz;
	return 0;
}

int bfree_msgrcv(int msqid, void *msgp, unsigned long msgsz, long msgtyp,
		 int msgflg)
{
	long *hdr = msgp;
	unsigned long n;

	(void)msgflg;
	(void)msgtyp;
	if (msqid < 0 || msqid >= BFREE_MSG_MAX || !bfree_msgs[msqid].in_use)
		return -EINVAL;
	if (msgp == NULL)
		return -EINVAL;
	if (bfree_msgs[msqid].len == 0)
		return -EAGAIN;
	n = bfree_msgs[msqid].len;
	if (n > msgsz)
		n = msgsz;
	hdr[0] = bfree_msgs[msqid].type;
	memcpy((char *)msgp + sizeof(long), bfree_msgs[msqid].buf, n);
	bfree_msgs[msqid].len = 0;
	return (int)n;
}

int bfree_msgctl(int msqid, int cmd, struct bfree_msqid_ds *buf)
{
	if (msqid < 0 || msqid >= BFREE_MSG_MAX || !bfree_msgs[msqid].in_use)
		return -EINVAL;
	if (cmd == BFREE_IPC_RMID) {
		bfree_msgs[msqid].in_use = 0;
		return 0;
	}
	if (cmd == BFREE_IPC_STAT && buf != NULL) {
		buf->msg_qnum = bfree_msgs[msqid].len ? 1 : 0;
		buf->msg_qbytes = BFREE_MSG_BUF;
		return 0;
	}
	return -EINVAL;
}
