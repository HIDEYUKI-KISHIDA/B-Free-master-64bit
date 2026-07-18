// 外部参照用プロトタイプ宣言
struct acpi_rsdp* find_rsdp(void);
// acpi_scan.c - RSDP→RSDT/XSDT→HPET探索の最小サンプル
// QEMU/PC互換機で動作するACPI HPETベースアドレス取得用
#include <stdint.h>
#include <stddef.h>

#define RSDP_SIGNATURE "RSD PTR "
#define HPET_SIGNATURE "HPET"

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

struct acpi_hpet_table {
    struct acpi_sdt_header header;
    uint8_t hardware_rev_id;
    uint8_t comparator_count : 5;
    uint8_t counter_size : 1;
    uint8_t reserved : 1;
    uint8_t legacy_replacement : 1;
    uint16_t pci_vendor_id;
    struct {
        uint8_t address_space_id;
        uint8_t register_bit_width;
        uint8_t register_bit_offset;
        uint8_t reserved;
        uint64_t address;
    } __attribute__((packed)) address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} __attribute__((packed));

// 物理メモリからRSDPを探索（0x000E0000～0x00100000）
struct acpi_rsdp* find_rsdp(void) {
    for (uintptr_t p = 0x000E0000; p < 0x00100000; p += 16) {
        char* sig = (char*)p;
        int found = 1;
        for (int i = 0; i < 8; ++i) {
            if (sig[i] != RSDP_SIGNATURE[i]) { found = 0; break; }
        }
        if (found) return (struct acpi_rsdp*)p;
    }
    return NULL;
}

// RSDT/XSDTからHPETテーブルを探索
static struct acpi_hpet_table* find_hpet_table(void) {
    struct acpi_rsdp* rsdp = find_rsdp();
    if (!rsdp) return NULL;
    struct acpi_sdt_header* xsdt = (struct acpi_sdt_header*)(uintptr_t)rsdp->xsdt_addr;
    if (!xsdt || xsdt->signature[0]!='X'||xsdt->signature[1]!='S'||xsdt->signature[2]!='D'||xsdt->signature[3]!='T') return NULL;
    int entries = (xsdt->length - sizeof(struct acpi_sdt_header)) / 8;
    uint64_t* entry_ptr = (uint64_t*)((uintptr_t)xsdt + sizeof(struct acpi_sdt_header));
    for (int i = 0; i < entries; ++i) {
        struct acpi_sdt_header* hdr = (struct acpi_sdt_header*)(uintptr_t)entry_ptr[i];
        if (hdr->signature[0]=='H'&&hdr->signature[1]=='P'&&hdr->signature[2]=='E'&&hdr->signature[3]=='T') {
            return (struct acpi_hpet_table*)hdr;
        }
    }
    return NULL;
}

// HPETベースアドレス取得API
uint64_t acpi_get_hpet_base(void) {
    struct acpi_hpet_table* hpet = find_hpet_table();
    if (!hpet) return 0;
    return hpet->address.address;
}
