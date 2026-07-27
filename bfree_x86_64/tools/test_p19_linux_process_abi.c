/*
 * P19_LINUX_PROCESS_ABI — Linux initial stack/auxv + uname=Linux (playbook L1/L1b).
 */
#include "linux_user_stack.h"
#include "syscall.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

struct guest_utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

int main(void)
{
	unsigned char region[8192];
	uintptr_t top = (uintptr_t)region + sizeof(region);
	char *argv[] = { "/bin/busybox", "ash", "-c", "echo ok", NULL };
	struct bfree_linux_auxinfo aux = {
		.phdr = 0x400040,
		.phent = 56,
		.phnum = 10,
		.entry = 0x40fdb0,
	};
	uintptr_t rsp;
	uint64_t *sp;
	struct guest_utsname uts;

	rsp = bfree_linux_user_stack_build(top, 4, argv, NULL, &aux);
	CHECK(rsp != 0, "stack build");
	CHECK((rsp & 0xF) == 0, "rsp 16-byte aligned");
	CHECK(rsp >= (uintptr_t)region && rsp < top, "rsp in region");

	sp = (uint64_t *)rsp;
	CHECK(sp[0] == 4, "argc");
	CHECK(strcmp((char *)(uintptr_t)sp[1], "/bin/busybox") == 0, "argv0");
	CHECK(strcmp((char *)(uintptr_t)sp[2], "ash") == 0, "argv1");
	CHECK(sp[5] == 0, "argv NULL");
	CHECK(sp[6] == 0, "envp NULL");

	{
		uint64_t *av = sp + 7;
		int saw_phdr = 0, saw_entry = 0, saw_null = 0;
		int i;

		for (i = 0; i < 32; i += 2) {
			if (av[i] == BFREE_AT_PHDR && av[i + 1] == aux.phdr)
				saw_phdr = 1;
			if (av[i] == BFREE_AT_ENTRY && av[i + 1] == aux.entry)
				saw_entry = 1;
			if (av[i] == BFREE_AT_NULL) {
				saw_null = 1;
				break;
			}
		}
		CHECK(saw_phdr, "AT_PHDR");
		CHECK(saw_entry, "AT_ENTRY");
		CHECK(saw_null, "AT_NULL");
	}

	guest_init();
	memset(&uts, 0, sizeof(uts));
	CHECK(bfree_invoke_syscall(63, (unsigned long)&uts, 0, 0, 0, 0, 0) == 0,
	      "uname");
	CHECK(strcmp(uts.sysname, "Linux") == 0, "sysname Linux");
	CHECK(strcmp(uts.machine, "x86_64") == 0, "machine");

	printf("P19_LINUX_PROCESS_ABI: PASS\n");
	return 0;
}
