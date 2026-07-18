#include <stddef.h>
#include <stdint.h>

#include "device.h"
#include "string.h"

#define NET0_RXBUF_SIZE 32
#define NET0_PKT_MAXLEN 1514

static uint8_t rxbuf[NET0_RXBUF_SIZE][NET0_PKT_MAXLEN];
static size_t rxlen[NET0_RXBUF_SIZE];
static int rx_head;
static int rx_tail;
static uint8_t local_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

void devnet0_push_rx(const uint8_t *pkt, size_t len)
{
    int next;

    if (pkt == NULL || len == 0 || len > NET0_PKT_MAXLEN)
        return;

    next = (rx_head + 1) % NET0_RXBUF_SIZE;
    if (next == rx_tail)
        return;

    memcpy(rxbuf[rx_head], pkt, len);
    rxlen[rx_head] = len;
    rx_head = next;
}

static int devnet0_open(void *dev, int mode)
{
    (void)dev;
    (void)mode;
    return 0;
}

static int devnet0_close(void *dev)
{
    (void)dev;
    return 0;
}

static ssize_t devnet0_read(void *dev, void *buf, size_t len)
{
    size_t packet_len;

    (void)dev;
    if (buf == NULL)
        return -1;
    if (rx_tail == rx_head)
        return 0;

    packet_len = rxlen[rx_tail];
    if (len < packet_len)
        return -1;

    memcpy(buf, rxbuf[rx_tail], packet_len);
    rx_tail = (rx_tail + 1) % NET0_RXBUF_SIZE;
    return (ssize_t)packet_len;
}

static ssize_t devnet0_write(void *dev, const void *buf, size_t len)
{
    extern int netdrv_send(const void *buf, unsigned int len);

    (void)dev;
    return (ssize_t)netdrv_send(buf, (unsigned int)len);
}

static int devnet0_ioctl(void *dev, int cmd, void *arg)
{
    (void)dev;
    if (cmd == 1 && arg != NULL) {
        memcpy(arg, local_mac, sizeof(local_mac));
        return 0;
    }
    return -1;
}

void register_devnet0(void)
{
    static struct device_ops ops = {
        .open = devnet0_open,
        .close = devnet0_close,
        .read = devnet0_read,
        .write = devnet0_write,
        .ioctl = devnet0_ioctl,
    };
    static device_t dev = {
        .name = "net0",
        .type = DEV_TYPE_OTHER,
        .ops = &ops,
        .priv = NULL,
        .next = NULL,
    };

    register_device(&dev);
    extern int devfs_register(const char *name, device_t *dev);
    devfs_register("net0", &dev);
}