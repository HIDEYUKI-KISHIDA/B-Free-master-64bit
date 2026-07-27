/*
 * P5_IPC_SHM — SysV shmget/shmat/shmdt/shmctl.
 */
#include "ipc_shm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	int shmid;
	char *addr;
	struct bfree_shmid_ds ds;

	bfree_ipc_reset();
	shmid = bfree_shmget(0, 64, 0666);
	CHECK(shmid > 0, "shmget");
	addr = bfree_shmat(shmid, NULL, 0);
	CHECK(addr != (void *)(intptr_t)-1, "shmat");
	strcpy(addr, "shm-data");
	CHECK(bfree_shmctl(shmid, BFREE_IPC_STAT, &ds) == 0, "shmctl stat");
	CHECK(ds.shm_nattch == 1, "nattch");
	CHECK(bfree_shmdt(addr) == 0, "shmdt");
	CHECK(bfree_shmctl(shmid, BFREE_IPC_RMID, NULL) == 0, "shmctl rmid");

	printf("P5_IPC_SHM: PASS\n");
	return 0;
}
