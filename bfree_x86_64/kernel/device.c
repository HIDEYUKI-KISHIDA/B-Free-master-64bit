

#include "device.h"
#include "string.h"
extern void uart_puts(const char *s);

// mouse.c等から呼ばれるデバイス登録API（ダミー実装）
int device_register(const char *name, device_ops_t *ops) {
    (void)name;
    (void)ops;
    return 0;
}

struct device *device_list = NULL;

// すべての登録済みデバイス名をシリアル出力
void print_all_devices(void) {
    uart_puts("[DEV] --- Registered Devices ---\n");
    device_t *dev = device_list;
    while (dev) {
        uart_puts("  - ");
        uart_puts(dev->name);
        uart_puts("\n");
        dev = dev->next;
    }
    uart_puts("[DEV] -------------------------\n");
}

// --- ダミー実装（リンクエラー回避用） ---
void register_sensor_device(int type) {(void)type;}
void register_rtc_device(int type) {(void)type;}
void register_tpm_device(volatile void *mmio_base, int version) {(void)mmio_base; (void)version;}
void register_acpi_device(volatile void *pm_base, int type) {(void)pm_base; (void)type;}
void register_audio_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type)
{ (void)mmio_base; (void)vendor_id; (void)device_id; (void)type; }
void register_net_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type)
{ (void)mmio_base; (void)vendor_id; (void)device_id; (void)type; }
void register_ahci_device(volatile void *abar, int port_num) {(void)abar; (void)port_num;}
void register_usb_controller(volatile void *mmio_base, int type) {(void)mmio_base; (void)type;}
void register_gpu_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type)
{ (void)mmio_base; (void)vendor_id; (void)device_id; (void)type; }
void register_serial_device(void) {
    uart_puts("[DEV] register_serial_device called\n");
    // 実際のシリアルデバイス登録処理はここに追加
}
void __attribute__((weak)) pci_scan_and_register_devices(void) {}

void register_device(device_t *dev) {
    dev->next = device_list;
    device_list = dev;
    uart_puts("[DEV] register_device: ");
    uart_puts(dev->name);
    uart_puts("\n");
}

void unregister_device(device_t *dev) {
    device_t **pp = &device_list;
    while (*pp) {
        if (*pp == dev) {
            *pp = dev->next;
            break;
        }
        pp = &((*pp)->next);
    }
}

device_t *find_device(const char *name) {
    device_t *dev = device_list;
    while (dev) {
        if (strcmp(dev->name, name) == 0) return dev;
        dev = dev->next;
    }
    return NULL;
}


