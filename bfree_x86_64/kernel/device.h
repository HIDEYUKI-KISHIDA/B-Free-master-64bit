// device_list本体へのアクセス用（print_all_devices用）
extern struct device *device_list;
#ifndef BFREE_DEVICE_H
#define BFREE_DEVICE_H


#include <stdint.h>
#include <stddef.h>

// ssize_t が未定義の場合は自前で定義
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

// デバイスタイプ定義
#define DEV_TYPE_CHAR   1
#define DEV_TYPE_BLOCK  2
#define DEV_TYPE_OTHER  3

// デバイス操作関数テーブル
struct device_ops {
    int (*open)(void *dev, int mode);
    int (*close)(void *dev);
    ssize_t (*read)(void *dev, void *buf, size_t len);
    ssize_t (*write)(void *dev, const void *buf, size_t len);
    int (*ioctl)(void *dev, int cmd, void *arg);
};
typedef struct device_ops device_ops_t;

// デバイス管理構造体（64ビット対応）
typedef struct device {
    const char *name;
    int type;
    struct device_ops *ops;
    void *priv; // デバイス固有データ
    struct device *next;
} device_t;

// デバイス管理API
void register_device(device_t *dev);
int device_register(const char *name, device_ops_t *ops);
void unregister_device(device_t *dev);
device_t *find_device(const char *name);
void print_all_devices(void);

// シリアルデバイス登録API
void register_serial_device(void);

// PCIデバイス列挙API
void pci_scan_and_register_devices(void);

// AHCI(SATA)デバイス登録API
void register_ahci_device(volatile void *abar, int port_num);

// USBコントローラ登録API
void register_usb_controller(volatile void *mmio_base, int type);

// ネットワークデバイス登録API
void register_net_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type);

// グラフィックデバイス登録API
void register_gpu_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type);

// サウンドデバイス登録API
void register_audio_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type);

// ACPI電源管理デバイス登録API
void register_acpi_device(volatile void *pm_base, int type);

// TPMセキュリティデバイス登録API
void register_tpm_device(volatile void *mmio_base, int version);

// RTCデバイス登録API
void register_rtc_device(int type);

// センサデバイス登録API
void register_sensor_device(int type);

#endif // BFREE_DEVICE_H
