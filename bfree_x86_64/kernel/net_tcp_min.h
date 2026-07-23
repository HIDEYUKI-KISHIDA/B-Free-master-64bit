#ifndef BFREE_NET_TCP_MIN_H
#define BFREE_NET_TCP_MIN_H

#include <stddef.h>
#include <stdint.h>

/* Minimal active-open TCP for QEMU slirp (10.0.2/24). One PCB family. */
#define BFREE_TCP_MAX 4

void tcp_min_init(void);
void tcp_min_input(uint32_t src_ip, const uint8_t *pkt, size_t len);

/* Returns pcb id (>=0) or negative errno-style code.
 * connect is non-blocking: SYN sent, handshake completed via tcp_min_pump/send/recv. */
int tcp_min_connect(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port);
int tcp_min_pump(int pcb); /* 0=ESTABLISHED, -115=EINPROGRESS, else error */
int tcp_min_send(int pcb, const uint8_t *data, size_t len);
int tcp_min_recv(int pcb, uint8_t *buf, size_t len);
void tcp_min_close(int pcb);
void tcp_min_poll(void);

#endif
