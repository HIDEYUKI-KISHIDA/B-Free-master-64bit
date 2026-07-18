// 型未定義エラー対策: 必ず最初にstdint.hをinclude
#include <stdint.h>
#ifndef ACPI_RSDP_DEF
#define ACPI_RSDP_DEF
struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));
#endif
// acpi_madt.c - ACPI MADT (APIC) テーブルパース最小サンプル
// LAPIC/IOAPIC/CPU/APIC ID情報をACPI MADTから取得
#include <stdint.h>
#include <stddef.h>

#define MADT_SIGNATURE "APIC"

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oemid[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t lapic_addr;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed));

// MADTサブテーブル型
#define MADT_TYPE_LOCAL_APIC      0
#define MADT_TYPE_IO_APIC         1
#define MADT_TYPE_INTERRUPT_SRC   2
#define MADT_TYPE_NMI_SRC         3
#define MADT_TYPE_LOCAL_APIC_NMI  4
#define MADT_TYPE_LOCAL_APIC_ADDR 5

struct madt_local_apic {
    uint8_t type;
    uint8_t length;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct madt_io_apic {
    uint8_t type;
    uint8_t length;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

// 外部参照: acpi_scan.cのfind_rsdp, acpi_sdt_header
extern struct acpi_rsdp* find_rsdp(void);

// MADTテーブル取得
static struct acpi_madt* find_madt(void) {
    struct acpi_rsdp* rsdp = find_rsdp();
    if (!rsdp) return NULL;
    struct acpi_sdt_header* xsdt = (struct acpi_sdt_header*)(uintptr_t)rsdp->xsdt_addr;
    if (!xsdt || xsdt->signature[0]!='X'||xsdt->signature[1]!='S'||xsdt->signature[2]!='D'||xsdt->signature[3]!='T') return NULL;
    int entries = (xsdt->length - sizeof(struct acpi_sdt_header)) / 8;
    uint64_t* entry_ptr = (uint64_t*)((uintptr_t)xsdt + sizeof(struct acpi_sdt_header));
    for (int i = 0; i < entries; ++i) {
        struct acpi_sdt_header* hdr = (struct acpi_sdt_header*)(uintptr_t)entry_ptr[i];
        if (hdr->signature[0]=='A'&&hdr->signature[1]=='P'&&hdr->signature[2]=='I'&&hdr->signature[3]=='C') {
            return (struct acpi_madt*)hdr;
        }
    }
    return NULL;
}

// LAPIC/IOAPIC/CPU/APIC ID情報を列挙
void acpi_enum_apic(void (*cb)(int type, void* entry)) {
    struct acpi_madt* madt = find_madt();
    if (!madt) return;
    uint8_t* p = madt->entries;
    uint8_t* end = (uint8_t*)madt + madt->header.length;
    while (p < end) {
        int type = p[0];
        int len = p[1];
        if (len < 2) break;
        cb(type, p);
        p += len;
    }
}

// LAPIC物理アドレス取得
uint32_t acpi_get_lapic_addr(void) {
    struct acpi_madt* madt = find_madt();
    if (!madt) return 0;
    return madt->lapic_addr;
}
