#include <stdint.h>
#include "device.h"
#include "gpu_backend.h"
#include "fbdev.h"
extern void uart_puts(const char *s);

// PCI構成空間アクセス
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// PCIデバイス情報
struct pci_device_info {
    uint8_t bus, dev, func;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if;
};

// PCI構成空間レジスタ読み出し
static uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = (1U << 31) | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t addr = (1U << 31) | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, addr);
    outl(PCI_CONFIG_DATA, value);
}

static uint64_t pci_read_bar_base(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, int *is64bit, int *is_io)
{
    uint32_t low = pci_config_read32(bus, dev, func, offset);
    uint64_t base;

    if (is_io) {
        *is_io = (low & 0x1U) != 0;
    }
    if ((low & 0x1U) != 0) {
        if (is64bit) {
            *is64bit = 0;
        }
        return (uint64_t)(low & ~0x3U);
    }

    if (is64bit) {
        *is64bit = ((low >> 1) & 0x3U) == 0x2U;
    }

    base = (uint64_t)(low & ~0xFU);
    if (((low >> 1) & 0x3U) == 0x2U) {
        uint32_t high = pci_config_read32(bus, dev, func, offset + 4);
        base |= ((uint64_t)high << 32);
    }

    return base;
}

static uint64_t pci_read_bar_size(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t original_low = pci_config_read32(bus, dev, func, offset);
    uint32_t original_high = 0;
    uint32_t size_low;
    uint32_t size_high = 0;
    uint64_t mask;

    if ((original_low & 0x1U) == 0 && (((original_low >> 1) & 0x3U) == 0x2U)) {
        original_high = pci_config_read32(bus, dev, func, offset + 4);
    }

    pci_config_write32(bus, dev, func, offset, 0xFFFFFFFFU);
    size_low = pci_config_read32(bus, dev, func, offset);
    pci_config_write32(bus, dev, func, offset, original_low);

    if ((original_low & 0x1U) != 0) {
        mask = (uint64_t)(size_low & ~0x3U);
        return mask != 0 ? (~mask + 1ULL) : 0;
    }

    if (((original_low >> 1) & 0x3U) == 0x2U) {
        pci_config_write32(bus, dev, func, offset + 4, 0xFFFFFFFFU);
        size_high = pci_config_read32(bus, dev, func, offset + 4);
        pci_config_write32(bus, dev, func, offset + 4, original_high);
        mask = ((uint64_t)size_high << 32) | (uint64_t)(size_low & ~0xFU);
    } else {
        mask = (uint64_t)(size_low & ~0xFU);
    }

    return mask != 0 ? (~mask + 1ULL) : 0;
}

static void pci_handle_display_controller(uint16_t vendor_id,
                                          uint16_t device_id,
                                          uint8_t class_code,
                                          uint8_t subclass,
                                          uint8_t bus,
                                          uint8_t dev,
                                          uint8_t func)
{
    static int g_display_controller_selected = 0;
    int type = 2;
    bfree_gpu_device_info_t gpu_device;
    int bar0_is64 = 0;
    int bar0_is_io = 0;
    int bar2_is64 = 0;
    int bar2_is_io = 0;
    uint64_t bar0_base = pci_read_bar_base(bus, dev, func, 0x10, &bar0_is64, &bar0_is_io);
    uint64_t bar2_base = pci_read_bar_base(bus, dev, func, 0x18, &bar2_is64, &bar2_is_io);
    uint64_t bar0_size = pci_read_bar_size(bus, dev, func, 0x10);
    uint64_t bar2_size = pci_read_bar_size(bus, dev, func, 0x18);
    volatile void *mmio_base = (volatile void *)(uintptr_t)(bar0_base);
    uintptr_t vram_base = (uintptr_t)(bar2_base);
    (void)bar0_is64;
    (void)bar2_is64;

    if (subclass == 0x00) type = 0;
    else if (subclass == 0x80) type = 1;

    if (bar0_is_io) {
        mmio_base = 0;
        bar0_size = 0;
    }
    if (bar2_is_io) {
        vram_base = 0;
        bar2_size = 0;
    }

    if (vendor_id == 0x1234 && device_id == 0x1111 && vram_base == 0) {
        vram_base = (uintptr_t)bar0_base;
        bar2_size = bar0_size;
    }

    gpu_device.vendor_id = vendor_id;
    gpu_device.device_id = device_id;
    gpu_device.class_code = class_code;
    gpu_device.subclass = subclass;
    gpu_device.mmio_base = (uintptr_t)mmio_base;
    gpu_device.vram_base = vram_base;
    gpu_device.mmio_size = (uint32_t)bar0_size;
    gpu_device.vram_size = (uint32_t)bar2_size;
    gpu_device.irq = -1;

    /* Stage 4: display controller の backend 選定入口はこの1箇所に固定する。 */
    if (!g_display_controller_selected) {
        if (gpu_backend_try_activate_gpu(&gpu_device) == 0) {
            uart_puts("[PCI] display backend=mmio-gpu\n");
        } else {
            uart_puts("[PCI] display backend=framebuffer (fallback)\n");
        }
        fbdev_refresh_backend_info();
        g_display_controller_selected = 1;
    }

    extern void register_gpu_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type);
    register_gpu_device(mmio_base, vendor_id, device_id, type);
}

// PCIデバイス列挙・登録
void pci_scan_and_register_devices(void) {
    gpu_backend_init();

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t vendor_id = pci_config_read32((uint8_t)bus, dev, func, 0x00) & 0xFFFF;
                if (vendor_id == 0xFFFF) continue;
                uint32_t reg2 = pci_config_read32((uint8_t)bus, dev, func, 0x08);
                uint16_t device_id = (pci_config_read32((uint8_t)bus, dev, func, 0x00) >> 16) & 0xFFFF;
                uint8_t class_code = (reg2 >> 24) & 0xFF;
                uint8_t subclass = (reg2 >> 16) & 0xFF;
                uint8_t prog_if = (reg2 >> 8) & 0xFF;

                // TPM（Trusted Platform Module）
                if (class_code == 0x0C && subclass == 0x05 && prog_if == 0x20) {
                    uint32_t bar0 = pci_config_read32((uint8_t)bus, dev, func, 0x10);
                    volatile void *mmio_base = (volatile void *)(uintptr_t)(bar0 & ~0xFU);
                    extern void register_tpm_device(volatile void *mmio_base, int version);
                    register_tpm_device(mmio_base, 2);
                }

                // ACPI電源管理コントローラ
                if (class_code == 0x0C && subclass == 0x05) {
                    uint32_t bar0 = pci_config_read32((uint8_t)bus, dev, func, 0x10);
                    volatile void *pm_base = (volatile void *)(uintptr_t)(bar0 & ~0xFU);
                    extern void register_acpi_device(volatile void *pm_base, int type);
                    register_acpi_device(pm_base, 0);
                }

                // サウンドコントローラ（HD Audio/AC97/USB Audio/その他）
                if (class_code == 0x04) {
                    int type = 3;
                    if (subclass == 0x03) type = 0;
                    else if (subclass == 0x01) type = 1;
                    else if (subclass == 0x80) type = 2;
                    uint32_t bar0 = pci_config_read32((uint8_t)bus, dev, func, 0x10);
                    volatile void *mmio_base = (volatile void *)(uintptr_t)(bar0 & ~0xFU);
                    extern void register_audio_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type);
                    register_audio_device(mmio_base, vendor_id, device_id, type);
                }

                // グラフィックコントローラ（VGA/GPU/その他）
                if (class_code == 0x03) {
                    pci_handle_display_controller(vendor_id, device_id, class_code, subclass, (uint8_t)bus, dev, func);
                }

                // ネットワークコントローラ（Ethernet/Wi-Fi/その他）
                if (class_code == 0x02) {
                    int type = 2;
                    if (subclass == 0x00) type = 0;
                    else if (subclass == 0x80) type = 1;
                    uint32_t bar0 = pci_config_read32((uint8_t)bus, dev, func, 0x10);
                    volatile void *mmio_base = (volatile void *)(uintptr_t)(bar0 & ~0xFU);
                    extern void register_net_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type);
                    register_net_device(mmio_base, vendor_id, device_id, type);
                }

                // AHCI (SATA) コントローラなら自動登録
                if (class_code == 0x01 && subclass == 0x06) {
                    uint32_t bar5 = pci_config_read32((uint8_t)bus, dev, func, 0x24);
                    volatile void *abar = (volatile void *)(uintptr_t)(bar5 & ~0xFU);
                    extern void register_ahci_device(volatile void *abar, int port_num);
                    register_ahci_device(abar, 0); // port_num=0仮
                }
                // USBコントローラ（XHCI/EHCI/OHCI/UHCI）
                if (class_code == 0x0C && subclass == 0x03) {
                    uint8_t prog_if = (reg2 >> 8) & 0xFF;
                    int type = -1;
                    if (prog_if == 0x30) type = 3; // XHCI
                    else if (prog_if == 0x20) type = 2; // EHCI
                    else if (prog_if == 0x10) type = 1; // OHCI
                    else if (prog_if == 0x00) type = 0; // UHCI
                    // BAR取得（XHCI/EHCIはBAR0, OHCI/UHCIはBAR4等）
                    uint32_t bar = pci_config_read32((uint8_t)bus, dev, func, (type >= 2) ? 0x10 : 0x20);
                    volatile void *mmio_base = (volatile void *)(uintptr_t)(bar & ~0xFU);
                    extern void register_usb_controller(volatile void *mmio_base, int type);
                    register_usb_controller(mmio_base, type);
                }
            }
        }
    }
}
