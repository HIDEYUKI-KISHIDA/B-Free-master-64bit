#include <stddef.h>
#include <stdint.h>
#include "string.h"
#include <stdbool.h>

#include "../../x86_64/kernel/netdrv.h"
#include "devnet0_runtime.h"
#include "sysmain/vmm.h"

void uart_puts(const char *s);
void uart_puthex64(uint64_t val);
void uart_puthex32(uint32_t val);

void uart_puts(const char *s);
void uart_puthex64(uint64_t val);

#define NETDRV_TXRING_SIZE 32
#define NETDRV_RXRING_SIZE 32
#define NETDRV_PKT_MAXLEN 1514

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define E1000_IOADDR 0x0000U
#define E1000_IODATA 0x0004U
#define E1000_CTRL 0x0000U
#define E1000_STATUS 0x0008U
#define E1000_IMC 0x00D8U
#define E1000_RCTL 0x0100U
#define E1000_TCTL 0x0400U
#define E1000_TIPG 0x0410U
#define E1000_RDBAL 0x2800U
#define E1000_RDBAH 0x2804U
#define E1000_RDLEN 0x2808U
#define E1000_RDH 0x2810U
#define E1000_RDT 0x2818U
#define E1000_TDBAL 0x3800U
#define E1000_TDBAH 0x3804U
#define E1000_TDLEN 0x3808U
#define E1000_TDH 0x3810U
#define E1000_TDT 0x3818U

#define E1000_CTRL_SLU 0x00000040U
#define E1000_CTRL_RST 0x04000000U
#define E1000_RCTL_EN 0x00000002U
#define E1000_RCTL_UPE 0x00000008U
#define E1000_RCTL_MPE 0x00000010U
#define E1000_RCTL_BAM 0x00008000U
#define E1000_RCTL_SECRC 0x04000000U
#define E1000_TCTL_EN 0x00000002U
#define E1000_TCTL_PSP 0x00000008U
#define E1000_TX_CMD_EOP 0x01U
#define E1000_TX_CMD_IFCS 0x02U
#define E1000_TX_CMD_RS 0x08U
#define E1000_TX_STATUS_DD 0x01U
#define E1000_RX_STATUS_DD 0x01U

#define PCI_COMMAND_IO 0x0001U
#define PCI_COMMAND_MEM 0x0002U
#define PCI_COMMAND_BUSMASTER 0x0004U

#define E1000_DESC_COUNT 8U

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    volatile uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t io_base;
    uint32_t mmio_base;
    int active;
    e1000_tx_desc_t *tx_descs;
    e1000_rx_desc_t *rx_descs;
    uint8_t *tx_buffers[E1000_DESC_COUNT];
    uint8_t *rx_buffers[E1000_DESC_COUNT];
    uint32_t tx_tail_index;
    uint32_t rx_index;
} e1000_runtime_t;

static uint8_t txring[NETDRV_TXRING_SIZE][NETDRV_PKT_MAXLEN];
static size_t txlen[NETDRV_TXRING_SIZE];
static int tx_head;
static int tx_tail;
static uint8_t rxring[NETDRV_RXRING_SIZE][NETDRV_PKT_MAXLEN];
static size_t rxlen[NETDRV_RXRING_SIZE];
static int rx_head;
static int rx_tail;
static int netdrv_backend = NETDRV_BACKEND_STUB;
static e1000_runtime_t e1000_runtime;
static uint32_t netdrv_logged_stub_tx;
static uint32_t netdrv_logged_stub_rx;

#ifndef NETDRV_HOST_TEST
static inline void netdrv_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline void netdrv_outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void netdrv_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t netdrv_inl(uint16_t port)
{
    uint32_t ret;

    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint16_t netdrv_inw(uint16_t port)
{
    uint16_t ret;

    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t netdrv_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t addr = (1U << 31)
        | ((uint32_t)bus << 16)
        | ((uint32_t)dev << 11)
        | ((uint32_t)func << 8)
        | (offset & 0xFCU);

    netdrv_outl(PCI_CONFIG_ADDRESS, addr);
    return netdrv_inl(PCI_CONFIG_DATA);
}

static void netdrv_pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t addr = (1U << 31)
        | ((uint32_t)bus << 16)
        | ((uint32_t)dev << 11)
        | ((uint32_t)func << 8)
        | (offset & 0xFCU);

    netdrv_outl(PCI_CONFIG_ADDRESS, addr);
    netdrv_outl(PCI_CONFIG_DATA, value);
}

static uint16_t netdrv_pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t value = netdrv_pci_read32(bus, dev, func, (uint8_t)(offset & 0xFCU));
    uint32_t shift = (uint32_t)(offset & 2U) * 8U;

    return (uint16_t)((value >> shift) & 0xffffU);
}

static void netdrv_pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value)
{
    uint32_t reg = netdrv_pci_read32(bus, dev, func, (uint8_t)(offset & 0xFCU));
    uint32_t shift = (uint32_t)(offset & 2U) * 8U;
    uint32_t mask = 0xffffU << shift;

    reg = (reg & ~mask) | ((uint32_t)value << shift);
    netdrv_pci_write32(bus, dev, func, (uint8_t)(offset & 0xFCU), reg);
}

static void netdrv_io_wait(void)
{
    netdrv_outb(0x80, 0);
}

static void e1000_write_reg(uint16_t reg, uint32_t value)
{
    if (e1000_runtime.mmio_base != 0) {
        *(volatile uint32_t *)((uintptr_t)e1000_runtime.mmio_base + reg) = value;
    } else {
        netdrv_outl((uint16_t)(e1000_runtime.io_base + E1000_IOADDR), reg);
        netdrv_outl((uint16_t)(e1000_runtime.io_base + E1000_IODATA), value);
    }
}

static uint32_t e1000_read_reg(uint16_t reg)
{
    if (e1000_runtime.mmio_base != 0) {
        return *(volatile uint32_t *)((uintptr_t)e1000_runtime.mmio_base + reg);
    } else {
        netdrv_outl((uint16_t)(e1000_runtime.io_base + E1000_IOADDR), reg);
        return netdrv_inl((uint16_t)(e1000_runtime.io_base + E1000_IODATA));
    }
}

static uint8_t e1000_dma_pool[20 * 4096] __attribute__((aligned(4096)));
static int e1000_dma_pool_idx = 0;

static void *netdrv_alloc_dma_page(void)
{
    if (e1000_dma_pool_idx < 20) {
        void *page = &e1000_dma_pool[e1000_dma_pool_idx * 4096];
        e1000_dma_pool_idx++;
        return page;
    }
    return NULL;
}

static int e1000_setup_dma(void)
{
    void *tx_desc_page = netdrv_alloc_dma_page();
    void *rx_desc_page = netdrv_alloc_dma_page();

    if (tx_desc_page == NULL || rx_desc_page == NULL)
        return -1;

    e1000_runtime.tx_descs = (e1000_tx_desc_t *)tx_desc_page;
    e1000_runtime.rx_descs = (e1000_rx_desc_t *)rx_desc_page;
    memset(e1000_runtime.tx_descs, 0, 4096);
    memset(e1000_runtime.rx_descs, 0, 4096);

    for (uint32_t index = 0; index < E1000_DESC_COUNT; ++index) {
        e1000_runtime.tx_buffers[index] = (uint8_t *)netdrv_alloc_dma_page();
        e1000_runtime.rx_buffers[index] = (uint8_t *)netdrv_alloc_dma_page();
        if (e1000_runtime.tx_buffers[index] == NULL || e1000_runtime.rx_buffers[index] == NULL)
            return -1;

        memset(e1000_runtime.tx_buffers[index], 0, 4096);
        memset(e1000_runtime.rx_buffers[index], 0, 4096);

        e1000_runtime.tx_descs[index].addr = (uint64_t)(uintptr_t)e1000_runtime.tx_buffers[index];
        e1000_runtime.tx_descs[index].status = E1000_TX_STATUS_DD;
        e1000_runtime.rx_descs[index].addr = (uint64_t)(uintptr_t)e1000_runtime.rx_buffers[index];
        e1000_runtime.rx_descs[index].status = 0;
    }

    e1000_runtime.tx_tail_index = 0;
    e1000_runtime.rx_index = 0;
    return 0;
}

static void e1000_init_rings(void)
{
    e1000_write_reg(E1000_TDBAL, (uint32_t)(uintptr_t)e1000_runtime.tx_descs);
    e1000_write_reg(E1000_TDBAH, 0);
    e1000_write_reg(E1000_TDLEN, E1000_DESC_COUNT * (uint32_t)sizeof(e1000_tx_desc_t));
    e1000_write_reg(E1000_TDH, 0);
    e1000_write_reg(E1000_TDT, 0);

    e1000_write_reg(E1000_RDBAL, (uint32_t)(uintptr_t)e1000_runtime.rx_descs);
    e1000_write_reg(E1000_RDBAH, 0);
    e1000_write_reg(E1000_RDLEN, E1000_DESC_COUNT * (uint32_t)sizeof(e1000_rx_desc_t));
    e1000_write_reg(E1000_RDH, 0);
    e1000_write_reg(E1000_RDT, E1000_DESC_COUNT - 1U);
}

static int e1000_hw_init(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint16_t command;
    uint32_t bar2;

    memset(&e1000_runtime, 0, sizeof(e1000_runtime));
    e1000_runtime.bus = bus;
    e1000_runtime.dev = dev;
    e1000_runtime.func = func;

    uint32_t bar0 = netdrv_pci_read32(bus, dev, func, 0x10);
    uint32_t bar1 = netdrv_pci_read32(bus, dev, func, 0x14);
    
    uart_puts("[NETDRV] e1000: BAR0="); uart_puthex64(bar0); uart_puts("\n");
    uart_puts("[NETDRV] e1000: BAR1="); uart_puthex64(bar1); uart_puts("\n");
    uart_puts("[NETDRV] e1000: BAR2="); uart_puthex64(netdrv_pci_read32(bus, dev, func, 0x18)); uart_puts("\n");
    uart_puts("[NETDRV] e1000: BAR3="); uart_puthex64(netdrv_pci_read32(bus, dev, func, 0x1C)); uart_puts("\n");

    if ((bar0 & 1U) != 0) {
        uart_puts("[NETDRV] e1000: BAR0 is not MMIO\n");
        return -1;
    }

    e1000_runtime.io_base = (uint16_t)(bar1 & ~0x3U);
    uint32_t mmio_phys = bar0 & ~0xFU;
    if (mmio_phys == 0) {
        uart_puts("[NETDRV] e1000: MMIO base is 0\n");
        return -1;
    }

    extern page_table_t kernel_page_table;
    if (vmm_map_mmio_huge(&kernel_page_table, mmio_phys, mmio_phys) != 0) {
        uart_puts("[NETDRV] e1000: failed to map MMIO\n");
        return -1;
    }
    e1000_runtime.mmio_base = mmio_phys;

    command = netdrv_pci_read16(bus, dev, func, 0x04);
    command |= (uint16_t)(PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUSMASTER);
    netdrv_pci_write16(bus, dev, func, 0x04, command);

    e1000_write_reg(E1000_CTRL, e1000_read_reg(E1000_CTRL) | E1000_CTRL_RST);
    for (int i = 0; i < 100000; ++i) {
        if ((e1000_read_reg(E1000_CTRL) & E1000_CTRL_RST) == 0)
            break;
        netdrv_io_wait();
    }

    if (e1000_setup_dma() != 0) {
        uart_puts("[NETDRV] e1000: setup_dma failed\n");
        return -1;
    }

    e1000_write_reg(E1000_IMC, 0xffffffffU);
    e1000_init_rings();
    e1000_write_reg(E1000_CTRL, e1000_read_reg(E1000_CTRL) | E1000_CTRL_SLU);
    e1000_write_reg(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    e1000_write_reg(E1000_TIPG, 0x0060200AU);
    e1000_write_reg(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (0x10U << 4) | (0x40U << 12));
    e1000_runtime.active = 1;
    uart_puts("[NETDRV] e1000 backend active\n");
    uart_puts("[NETDRV] e1000 CTRL="); uart_puthex64((uint64_t)e1000_read_reg(E1000_CTRL));
    uart_puts(" TCTL="); uart_puthex64((uint64_t)e1000_read_reg(E1000_TCTL));
    uart_puts("\n");
    uart_puts("[NETDRV] e1000 mmio_base=");
    uart_puthex64((uint64_t)e1000_runtime.mmio_base);
    uart_puthex64((uint64_t)e1000_runtime.io_base);
    uart_puts(" tx_desc=");
    uart_puthex64((uint64_t)(uintptr_t)e1000_runtime.tx_descs);
    uart_puts(" rx_desc=");
    uart_puthex64((uint64_t)(uintptr_t)e1000_runtime.rx_descs);
    uart_puts("\n");
    return 0;
}

static int e1000_send_packet(const void *buf, unsigned int len)
{
    e1000_tx_desc_t *desc;
    uint32_t index = e1000_runtime.tx_tail_index;
    uint32_t spin;
    static uint32_t logged_e1000_tx;

    if (!e1000_runtime.active || buf == NULL || len == 0 || len > 2048U) {
        uart_puts("[NETDRV] e1000_send_packet: bad params\n");
        return -1;
    }

    desc = &e1000_runtime.tx_descs[index];
    // Removed the DD check here because we wait synchronously for DD below,
    // and QEMU might have cleared the initial DD we set.

    unsigned int copy_len = len;
    if (len < 60) {
        memset((void*)(e1000_runtime.tx_buffers[index] + len), 0, 60 - len);
        len = 60;
    }
    memcpy(e1000_runtime.tx_buffers[index], buf, copy_len);
    desc->length = (uint16_t)len;
    desc->cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    desc->status = 0;

    e1000_runtime.tx_tail_index = (index + 1U) % E1000_DESC_COUNT;
    e1000_write_reg(E1000_TDT, e1000_runtime.tx_tail_index);

    if (logged_e1000_tx < 8U) {
        uart_puts("[NETDRV] e1000 tx len=");
        uart_puthex64((uint64_t)len);
        uart_puts(" slot=");
        uart_puthex64((uint64_t)index);
        uart_puts("\n");
        for (unsigned int i = 0; i < len && i < 64; i++) {
            uart_puts(" ");
            uart_puthex64((uint64_t)((const uint8_t*)buf)[i]);
        }
        uart_puts("\n");
        logged_e1000_tx += 1U;
    }

    for (spin = 0; spin < 1000000U; ++spin) {
        if ((desc->status & E1000_TX_STATUS_DD) != 0) {
            return (int)len;
        }
    }

    uart_puts("[NETDRV] e1000 tx timeout slot=");
    uart_puthex64((uint64_t)index);
    uart_puts(" TDH=");
    uart_puthex64((uint64_t)e1000_read_reg(E1000_TDH));
    uart_puts(" TDT=");
    uart_puthex64((uint64_t)e1000_read_reg(E1000_TDT));
    uart_puts(" CMD=");
    uart_puthex64((uint64_t)desc->cmd);
    uart_puts("\n");

    return -1;
}

static int e1000_recv_packet(void *buf, unsigned int maxlen)
{
    e1000_rx_desc_t *desc;
    uint32_t index = e1000_runtime.rx_index;
    uint16_t packet_len;
    static uint32_t logged_e1000_rx;

    if (!e1000_runtime.active || buf == NULL)
        return -1;

    desc = &e1000_runtime.rx_descs[index];
    if ((desc->status & E1000_RX_STATUS_DD) == 0)
        return 0;

    packet_len = desc->length;
    if (packet_len > maxlen)
        return -1;

    memcpy(buf, e1000_runtime.rx_buffers[index], packet_len);
    if (logged_e1000_rx < 8U) {
        uart_puts("[NETDRV] e1000 rx len=");
        uart_puthex64((uint64_t)packet_len);
        uart_puts(" slot=");
        uart_puthex64((uint64_t)index);
        uart_puts("\n");
        logged_e1000_rx += 1U;
    }
    desc->status = 0;
    desc->length = 0;
    e1000_write_reg(E1000_RDT, index);
    e1000_runtime.rx_index = (index + 1U) % E1000_DESC_COUNT;
    return (int)packet_len;
}

static int netdrv_detect_backend(void)
{
    static const struct {
        uint16_t vendor_id;
        uint16_t device_id;
        int backend;
        const char *label;
        uint8_t bus;
        uint8_t dev;
        uint8_t func;
    } candidates[] = {
        {0x8086, 0x100e, NETDRV_BACKEND_E1000, "[NETDRV] PCI e1000 detected\n", 0, 0, 0},
        {0x8086, 0x100f, NETDRV_BACKEND_E1000, "[NETDRV] PCI e1000 detected\n", 0, 0, 0},
        {0x8086, 0x1019, NETDRV_BACKEND_E1000, "[NETDRV] PCI e1000 detected\n", 0, 0, 0},
        {0x10ec, 0x8139, NETDRV_BACKEND_RTL8139, "[NETDRV] PCI rtl8139 detected; runtime still using ring stub\n", 0, 0, 0},
        {0x1af4, 0x1000, NETDRV_BACKEND_VIRTIO_NET_PCI, "[NETDRV] PCI virtio-net detected; runtime still using ring stub\n", 0, 0, 0},
        {0x1af4, 0x1041, NETDRV_BACKEND_VIRTIO_NET_PCI, "[NETDRV] PCI virtio-net detected; runtime still using ring stub\n", 0, 0, 0},
    };
    uint16_t vendor_id;

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t reg0 = netdrv_pci_read32((uint8_t)bus, dev, func, 0x00);
                uint16_t device_id;

                vendor_id = (uint16_t)(reg0 & 0xffffU);
                if (vendor_id == 0xffffU)
                    continue;

                device_id = (uint16_t)((reg0 >> 16) & 0xffffU);
                for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
                    if (candidates[i].vendor_id == vendor_id && candidates[i].device_id == device_id) {
                        if (candidates[i].backend == NETDRV_BACKEND_E1000) {
                            uart_puts(candidates[i].label);
                            if (e1000_hw_init((uint8_t)bus, dev, func) == 0)
                                return candidates[i].backend;
                            uart_puts("[NETDRV] e1000 init failed; using ring stub\n");
                            return NETDRV_BACKEND_STUB;
                        }
                        uart_puts(candidates[i].label);
                        return candidates[i].backend;
                    }
                }
            }
        }
    }

    return NETDRV_BACKEND_STUB;
}
#endif

int netdrv_backend_kind(void)
{
    return netdrv_backend;
}

int netdrv_init(void)
{
    tx_head = 0;
    tx_tail = 0;
    rx_head = 0;
    rx_tail = 0;
    memset(txring, 0, sizeof(txring));
    memset(txlen, 0, sizeof(txlen));
    memset(rxring, 0, sizeof(rxring));
    memset(rxlen, 0, sizeof(rxlen));
    netdrv_logged_stub_tx = 0;
    netdrv_logged_stub_rx = 0;

#ifdef NETDRV_HOST_TEST
    netdrv_backend = NETDRV_BACKEND_STUB;
#else
    netdrv_backend = netdrv_detect_backend();
    if (netdrv_backend == NETDRV_BACKEND_STUB)
        uart_puts("[NETDRV] no supported PCI NIC detected; using ring stub\n");
#endif

    return 0;
}

int netdrv_send(const void *buf, unsigned int len)
{
    int next;

    if (buf == NULL || len == 0 || len > NETDRV_PKT_MAXLEN)
        return -1;

#ifndef NETDRV_HOST_TEST
    if (netdrv_backend == NETDRV_BACKEND_E1000 && e1000_runtime.active)
        return e1000_send_packet(buf, len);
#endif

    if (netdrv_logged_stub_tx < 4U) {
        uart_puts("[NETDRV] stub tx len=");
        uart_puthex64((uint64_t)len);
        uart_puts("\n");
        netdrv_logged_stub_tx += 1U;
    }

    next = (tx_head + 1) % NETDRV_TXRING_SIZE;
    if (next == tx_tail)
        return -1;

    memcpy(txring[tx_head], buf, len);
    txlen[tx_head] = len;
    tx_head = next;
    return (int)len;
}

int netdrv_recv(void *buf, unsigned int maxlen)
{
    size_t packet_len;
    int ret;

    if (buf == NULL)
        return -1;

#ifndef NETDRV_HOST_TEST
    if (netdrv_backend == NETDRV_BACKEND_E1000 && e1000_runtime.active)
        return e1000_recv_packet(buf, maxlen);
#endif

    if (rx_tail == rx_head)
        return 0;

    packet_len = rxlen[rx_tail];
    if (maxlen < packet_len)
        return -1;

    memcpy(buf, rxring[rx_tail], packet_len);
    rx_tail = (rx_tail + 1) % NETDRV_RXRING_SIZE;
    ret = (int)packet_len;
    if (netdrv_logged_stub_rx < 4U) {
        uart_puts("[NETDRV] stub rx len=");
        uart_puthex64((uint64_t)packet_len);
        uart_puts("\n");
        netdrv_logged_stub_rx += 1U;
    }
    return ret;
}

void netdrv_push_rx(const uint8_t *pkt, size_t len)
{
    int next;

    if (pkt == NULL || len == 0 || len > NETDRV_PKT_MAXLEN)
        return;

    next = (rx_head + 1) % NETDRV_RXRING_SIZE;
    if (next == rx_tail)
        return;

    memcpy(rxring[rx_head], pkt, len);
    rxlen[rx_head] = len;
    rx_head = next;

    extern void devnet0_push_rx(const uint8_t *pkt, size_t len);
    devnet0_push_rx(pkt, len);
}