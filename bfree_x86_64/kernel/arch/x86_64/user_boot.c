#include "user_boot.h"

#include <stddef.h>

void *memcpy(void *dst, const void *src, unsigned long n);

static int in_ring0(void)
{
	uint16_t cs;

	__asm__ volatile("mov %%cs, %0" : "=r"(cs));
	return (cs & 3U) == 0U;
}

int bfree_user_payload_install(const void *blob, size_t len)
{
	void *dest = (void *)BFREE_USER_LOAD_ADDR;

	if (blob == NULL || len == 0)
		return -1;
	if (!in_ring0())
		return -2;

	memcpy(dest, blob, len);
	return 0;
}
