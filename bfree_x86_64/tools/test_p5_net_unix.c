/*
 * P5_NET_UNIX — AF_UNIX socketpair stream I/O.
 */
#include "net_unix.h"

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
	int sv[2];
	char buf[32];

	bfree_net_reset();
	CHECK(bfree_socketpair(1, 1, 0, sv) == 0, "socketpair");
	CHECK(bfree_sendto(sv[0], "unix-pkt", 8, 0, NULL, 0) == 8, "send");
	memset(buf, 0, sizeof(buf));
	CHECK(bfree_recvfrom(sv[1], buf, sizeof(buf), 0, NULL, NULL) == 8,
	      "recv");
	CHECK(memcmp(buf, "unix-pkt", 8) == 0, "payload");

	printf("P5_NET_UNIX: PASS\n");
	return 0;
}
