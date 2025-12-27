/*

B-Free Project - GNU Generic PUBLIC LICENSE

64-bit GDT and IDT definitions

*/

#ifndef __GDT_IDT_64_H__
#define __GDT_IDT_64_H__

#include "types.h"

/* GDT segment descriptor (64-bit) */
typedef struct {
	UWORD16 limit_low;
	UWORD16 base_low;
	BYTE base_mid;
	BYTE attr1;
	BYTE limit_high_attr2;
	BYTE base_high;
} gdt_descriptor_t;

/* TSS descriptor (64-bit) - requires 16 bytes */
typedef struct {
	UWORD16 limit_low;
	UWORD16 base_low;
	BYTE base_mid;
	BYTE attr1;
	BYTE limit_high_attr2;
	BYTE base_high;
	UWORD32 base_upper;
	UWORD32 reserved;
} tss_descriptor_64_t;

/* IDT Gate Descriptor (64-bit) */
typedef struct {
	UWORD16 offset_low;
	UWORD16 selector;
	BYTE ist;		/* IST (Interrupt Stack Table) */
	BYTE type_attr;		/* Type and attributes */
	UWORD16 offset_mid;
	UWORD32 offset_high;
	UWORD32 reserved;
} idt_gate_64_t;

/* Task State Segment (64-bit) */
typedef struct {
	UWORD32 reserved1;
	UWORD64 rsp0;		/* Ring 0 stack pointer */
	UWORD64 rsp1;		/* Ring 1 stack pointer */
	UWORD64 rsp2;		/* Ring 2 stack pointer */
	UWORD64 reserved2;
	UWORD64 ist1;		/* IST1-7 */
	UWORD64 ist2;
	UWORD64 ist3;
	UWORD64 ist4;
	UWORD64 ist5;
	UWORD64 ist6;
	UWORD64 ist7;
	UWORD64 reserved3;
	UWORD16 reserved4;
	UWORD16 iopb;		/* IO Map Base Address */
} tss_t;

/* GDT indices */
#define GDT_NULL	0	/* Null selector */
#define GDT_CODE64	1	/* 64-bit code */
#define GDT_DATA64	2	/* 64-bit data */
#define GDT_TSS		3	/* Task State Segment */
#define GDT_SIZE	5

/* Descriptor attributes */
#define DESC_ACCESS	0x01	/* Accessed */
#define DESC_WRITE	0x02	/* Writable (data) / Readable (code) */
#define DESC_DOWN	0x04	/* Direction (data) / Conforming (code) */
#define DESC_EXEC	0x08	/* Executable (code) / Direction (data) */
#define DESC_S		0x10	/* Descriptor type (1=code/data) */
#define DESC_DPL	0x60	/* Descriptor Privilege Level */
#define DESC_P		0x80	/* Present */
#define DESC_AVL	0x10	/* Available for system software */
#define DESC_L		0x20	/* Long mode (64-bit) */
#define DESC_DB		0x40	/* Default operation size */
#define DESC_G		0x80	/* Granularity (4KB) */

/* IDT Gate type */
#define IDT_INTERRUPT	0x0E	/* Interrupt gate */
#define IDT_TRAP	0x0F	/* Trap gate */

void	setup_gdt64(void);
void	setup_idt64(void);
void	load_gdt64(gdt_descriptor_t *gdt, UWORD16 limit);
void	load_idt64(idt_gate_64_t *idt, UWORD16 limit);

#endif /* __GDT_IDT_64_H__ */
