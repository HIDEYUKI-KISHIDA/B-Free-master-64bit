/*
 * P4_CLONE_FUTEX — CLONE_VM|CLONE_THREAD and futex wake.
 */
#include "process.h"
#include "thread.h"

#include <stdio.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

static int shared_counter;
static int futex_word;

static int thread_inc(void *arg)
{
	int *p = arg;

	(*p)++;
	bfree_futex(&futex_word, BFREE_FUTEX_WAKE, 1, NULL);
	return 0;
}

int main(void)
{
	struct bfree_proc_mgr mgr;
	int tid;

	bfree_proc_init(&mgr);
	shared_counter = 0;
	futex_word = 1;

	tid = bfree_clone(&mgr, BFREE_CLONE_VM | BFREE_CLONE_THREAD,
			  NULL, NULL, NULL, NULL, thread_inc, &shared_counter);
	CHECK(tid > 1, "clone thread");
	CHECK(bfree_thread_run(&mgr, tid) == 0, "run thread");
	CHECK(shared_counter == 1, "shared memory updated");
	CHECK(bfree_futex(&futex_word, BFREE_FUTEX_WAKE, 1, NULL) >= 0,
	      "futex wake");

	printf("P4_CLONE_FUTEX: PASS\n");
	return 0;
}
