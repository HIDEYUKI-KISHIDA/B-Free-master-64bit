#include "linux_user_stack.h"

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *s, int c, unsigned long n);

#define STACK_GUARD_BYTES  256UL
#define STACK_RAND_BYTES   16UL

static size_t cstr_len(const char *s)
{
	size_t n = 0;

	if (s == NULL)
		return 0;
	while (s[n] != '\0')
		n++;
	return n;
}

static int count_ptrs(char *const v[])
{
	int n = 0;

	if (v == NULL)
		return 0;
	while (v[n] != NULL)
		n++;
	return n;
}

uintptr_t bfree_linux_user_stack_build(uintptr_t stack_top, int argc,
				       char *const argv[], char *const envp[],
				       const struct bfree_linux_auxinfo *aux)
{
	int envc;
	size_t str_bytes = 0;
	size_t vec_bytes;
	size_t aux_bytes;
	size_t total;
	uintptr_t region;
	char *str;
	size_t str_off = 0;
	uint64_t *sp;
	uintptr_t rsp;
	int i;
	static const unsigned char fake_rand[STACK_RAND_BYTES] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
	};
	uintptr_t rand_addr;

	if (stack_top < 0x10000UL || argc < 0 || aux == NULL)
		return 0;
	if (argc > 0 && argv == NULL)
		return 0;

	envc = count_ptrs(envp);
	for (i = 0; i < argc; i++) {
		if (argv[i] == NULL)
			return 0;
		str_bytes += cstr_len(argv[i]) + 1U;
	}
	for (i = 0; i < envc; i++)
		str_bytes += cstr_len(envp[i]) + 1U;
	str_bytes += STACK_RAND_BYTES;

	/* argc + argv*(argc+1) + envp*(envc+1) */
	vec_bytes = sizeof(uint64_t) *
		    (1UL + (size_t)argc + 1UL + (size_t)envc + 1UL);
	/* 12 aux pairs + AT_NULL */
	aux_bytes = sizeof(uint64_t) * 2UL * 13UL;
	total = vec_bytes + aux_bytes + str_bytes + STACK_GUARD_BYTES + 16UL;
	if (total >= stack_top)
		return 0;

	region = stack_top - total;
	str = (char *)(uintptr_t)region;
	memset(str, 0, (unsigned long)total);

	memcpy(str + str_off, fake_rand, STACK_RAND_BYTES);
	rand_addr = (uintptr_t)(str + str_off);
	str_off += STACK_RAND_BYTES;

	/* Place vectors at high end, strings at low end. */
	rsp = (stack_top - (vec_bytes + aux_bytes)) & ~0xFUL;
	sp = (uint64_t *)(uintptr_t)rsp;

	sp[0] = (uint64_t)argc;
	for (i = 0; i < argc; i++) {
		size_t len = cstr_len(argv[i]) + 1U;

		memcpy(str + str_off, argv[i], len);
		sp[1 + i] = (uint64_t)(uintptr_t)(str + str_off);
		str_off += len;
	}
	sp[1 + argc] = 0;
	for (i = 0; i < envc; i++) {
		size_t len = cstr_len(envp[i]) + 1U;

		memcpy(str + str_off, envp[i], len);
		sp[2 + argc + i] = (uint64_t)(uintptr_t)(str + str_off);
		str_off += len;
	}
	sp[2 + argc + envc] = 0;

	{
		uint64_t *av = sp + 3 + argc + envc;
		int k = 0;

		av[k++] = BFREE_AT_PHDR;
		av[k++] = aux->phdr;
		av[k++] = BFREE_AT_PHENT;
		av[k++] = aux->phent;
		av[k++] = BFREE_AT_PHNUM;
		av[k++] = aux->phnum;
		av[k++] = BFREE_AT_PAGESZ;
		av[k++] = 4096;
		av[k++] = BFREE_AT_BASE;
		av[k++] = 0;
		av[k++] = BFREE_AT_FLAGS;
		av[k++] = 0;
		av[k++] = BFREE_AT_ENTRY;
		av[k++] = aux->entry;
		av[k++] = BFREE_AT_UID;
		av[k++] = 0;
		av[k++] = BFREE_AT_EUID;
		av[k++] = 0;
		av[k++] = BFREE_AT_GID;
		av[k++] = 0;
		av[k++] = BFREE_AT_EGID;
		av[k++] = 0;
		av[k++] = BFREE_AT_SECURE;
		av[k++] = 0;
		av[k++] = BFREE_AT_RANDOM;
		av[k++] = rand_addr;
		av[k++] = BFREE_AT_NULL;
		av[k++] = 0;
		(void)k;
	}

	return rsp;
}
