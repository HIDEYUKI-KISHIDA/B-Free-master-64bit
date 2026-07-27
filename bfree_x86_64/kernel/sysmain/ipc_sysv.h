#ifndef BFREE_IPC_SYSV_H
#define BFREE_IPC_SYSV_H

#define BFREE_IPC_RMID 0
#define BFREE_IPC_STAT 2
#define BFREE_IPC_SET  1
#define BFREE_IPC_CREAT 01000

struct bfree_semid_ds {
	unsigned short sem_nsems;
};

struct bfree_msqid_ds {
	unsigned long msg_qnum;
	unsigned long msg_qbytes;
};

int bfree_semget(int key, int nsems, int semflg);
int bfree_semop(int semid, short *sops, unsigned nsops);
int bfree_semctl(int semid, int semnum, int cmd, void *arg);

int bfree_msgget(int key, int msgflg);
int bfree_msgsnd(int msqid, const void *msgp, unsigned long msgsz, int msgflg);
int bfree_msgrcv(int msqid, void *msgp, unsigned long msgsz, long msgtyp,
		 int msgflg);
int bfree_msgctl(int msqid, int cmd, struct bfree_msqid_ds *buf);

void bfree_ipc_sysv_reset(void);

#endif
