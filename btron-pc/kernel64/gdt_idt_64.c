// gdt_idt_64.c - 64ビット用GDT/IDT初期化雛形
#include "../include64/types.h"


// 64ビットGDT/IDT初期化の最小実装例
typedef struct {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char  base_middle;
    unsigned char  access;
    unsigned char  granularity;
    unsigned char  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    unsigned short limit;
    unsigned long  base;
} __attribute__((packed)) gdt_ptr_t;

gdt_entry_t gdt[3];
gdt_ptr_t   gdt_ptr;

void setup_gdt64(void) {
    // 仮のGDT初期化（本実装は要拡張）
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 3) - 1;
    gdt_ptr.base  = (unsigned long)&gdt;
    // lgdt命令はアセンブラで実装する必要あり
}

typedef struct {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char  ist;
    unsigned char  type_attr;
    unsigned short offset_middle;
    unsigned int   offset_high;
    unsigned int   zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    unsigned short limit;
    unsigned long  base;
} __attribute__((packed)) idt_ptr_t;

idt_entry_t idt[256];
idt_ptr_t   idt_ptr;

void setup_idt64(void) {
    // 仮のIDT初期化（本実装は要拡張）
    idt_ptr.limit = (sizeof(idt_entry_t) * 256) - 1;
    idt_ptr.base  = (unsigned long)&idt;
    // lidt命令はアセンブラで実装する必要あり
}
