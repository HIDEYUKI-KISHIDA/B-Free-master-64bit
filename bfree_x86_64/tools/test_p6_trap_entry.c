/*
 * P6_TRAP_ENTRY — syscall trap stub (Linux register convention).
 */
#include "fs_ofd.h"
#include "syscall.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

static long trap_invoke(unsigned long nr, unsigned long a0, unsigned long a1,
			unsigned long a2)
{
	unsigned long ret;

	__asm__ volatile(
		"mov %[nr], %%rax\n"
		"mov %[a0], %%rdi\n"
		"mov %[a1], %%rsi\n"
		"mov %[a2], %%rdx\n"
		"xor %%r10, %%r10\n"
		"xor %%r8, %%r8\n"
		"xor %%r9, %%r9\n"
		"call bfree_trap_syscall_entry\n"
		"mov %%rax, %[ret]\n"
		: [ret] "=r"(ret)
		: [nr] "r"(nr), [a0] "r"(a0), [a1] "r"(a1), [a2] "r"(a2)
		: "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx",
		  "memory");

	return (long)ret;
}

int main(void)
{
	struct bfree_fs *fs;
	char buf[16];
	long fd;
	long rc;

	guest_init();
	fs = guest_fs();

	CHECK(bfree_create(fs, "/tmp/trap.txt", 0644) == 0, "create");
	fd = trap_invoke(2, (unsigned long)"/tmp/trap.txt", 0, 0644);
	CHECK(fd >= 0, "trap open");
	rc = trap_invoke(1, (unsigned long)fd, (unsigned long)"trap", 4);
	CHECK(rc == 4, "trap write");
	rc = trap_invoke(8, (unsigned long)fd, 0, 0);
	CHECK(rc == 0, "trap lseek");
	memset(buf, 0, sizeof(buf));
	rc = trap_invoke(0, (unsigned long)fd, (unsigned long)buf, 4);
	CHECK(rc == 4, "trap read");
	CHECK(memcmp(buf, "trap", 4) == 0, "payload");

	printf("P6_TRAP_ENTRY: PASS\n");
	return 0;
}
