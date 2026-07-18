#include <stdint.h>
#include "device.h"

// ACPI電源管理・システム制御デバイス雛形
// PCIスキャンでclass_code==0x0C, subclass==0x05（Power Mgmt）、またはACPIテーブル直接探索

struct acpi_info {
    volatile void *pm_base;
    int type; // 0:ACPI, 1:APM, 2:Other
};

void register_acpi_device(volatile void *pm_base, int type) {
    static struct acpi_info priv;
    priv.pm_base = pm_base;
    priv.type = type;
    static struct device_ops ops = {
        .open = NULL, // 実装予定
        .close = NULL,
        .read = NULL,
        .write = NULL,
        .ioctl = NULL
    };
    static device_t acpi_dev = {
        .name = "acpi0",
        .type = DEV_TYPE_OTHER,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&acpi_dev);
}
