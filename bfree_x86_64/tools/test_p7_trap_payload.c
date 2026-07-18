/*
 * P7_TRAP_PAYLOAD — static ET_EXEC using trap entry for write/exit.
 */
#include "syscall.h"
#include "trap_setup.h"

#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

#ifndef TRAP_PAYLOAD_ELF
#define TRAP_PAYLOAD_ELF "build/trap_payload.elf"
#endif

int main(void)
{
	pid_t pid;
	int st;

	guest_init();
	bfree_trap_init();

	pid = fork();
	CHECK(pid >= 0, "fork");
	if (pid == 0) {
		execl(TRAP_PAYLOAD_ELF, "trap_payload", (char *)NULL);
		_exit(127);
	}
	CHECK(waitpid(pid, &st, 0) == pid, "wait");
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "payload exit 0");

	printf("P7_TRAP_PAYLOAD: PASS\n");
	return 0;
}
