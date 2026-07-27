/*
 * P7_TRAP_SETUP — IDT / syscall MSR state initialization.
 */
#include "trap_setup.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	const struct bfree_trap_state *st;

	bfree_trap_init();
	CHECK(bfree_trap_is_ready() != 0, "ready");
	st = bfree_trap_state();
	CHECK(st != NULL, "state");
	CHECK(st->syscall_lstar != 0, "lstar");
	CHECK(st->idtr.limit > 0, "idtr limit");
	CHECK(st->gates[0x80].type_attr == 0x8e, "syscall gate");

	printf("P7_TRAP_SETUP: PASS\n");
	return 0;
}
